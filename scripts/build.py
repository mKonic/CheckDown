#!/usr/bin/env python3
"""CheckDown build script.

Usage:
  python scripts/build.py              # Release build
  python scripts/build.py --installer  # Release build + NSIS installer
  python scripts/build.py --all        # Release build + NSIS installer + extension zip
  python scripts/build.py --update-ytdlp  # Update yt-dlp to latest before building
"""

import os
import sys
import glob
import json
import zipfile
import subprocess
import urllib.request
from pathlib import Path

# ---------------------------------------------------------------------------
# Config — edit paths here or set env vars QT_DIR / CURL_DIR
# ---------------------------------------------------------------------------
ROOT     = Path(__file__).resolve().parent.parent
QT_DIR   = Path(os.environ.get("QT_DIR",   str(ROOT / "deps/qt/6.8.3/msvc2022_64")))
MOC      = QT_DIR / "bin/moc.exe"
RCC      = QT_DIR / "bin/rcc.exe"
VCVARS   = Path("C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvarsall.bat")
MSBUILD  = Path("C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/MSBuild/Current/Bin/amd64/MSBuild.exe")
NSIS     = Path("C:/Program Files (x86)/NSIS/makensis.exe")
VCXPROJ  = ROOT / "build/CheckDown.vcxproj"
NSI      = ROOT / "installer/checkdown.nsi"
GEN_DIR  = ROOT / "generated"
SRC_DIR  = ROOT / "src"

# ---------------------------------------------------------------------------

def run(cmd, **kwargs):
    print(f"  > {cmd if isinstance(cmd, str) else ' '.join(str(c) for c in cmd)}")
    r = subprocess.run(cmd, **kwargs)
    if r.returncode != 0:
        print(f"\nFAILED (exit {r.returncode})")
        sys.exit(r.returncode)
    return r


def get_vs_env():
    """Run vcvarsall x64 and capture the resulting environment."""
    result = subprocess.run(
        f'cmd /c ""{VCVARS}" x64 && set"',
        capture_output=True, text=True, shell=True,
    )
    env = {}
    for line in result.stdout.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            env[k] = v
    if not env:
        print("ERROR: vcvarsall failed or produced no output. Is VS Build Tools installed?")
        sys.exit(1)
    return env


def step_moc():
    print("\n[1/4] Generating MOC files...")
    GEN_DIR.mkdir(exist_ok=True)
    count = 0
    for h in SRC_DIR.rglob("*.h"):
        if "Q_OBJECT" in h.read_text(encoding="utf-8", errors="ignore"):
            out = GEN_DIR / f"moc_{h.stem}.cpp"
            run([str(MOC), str(h), "-o", str(out)])
            count += 1
    print(f"  MOC'd {count} header(s)")


def step_rcc():
    print("\n[2/4] Compiling resources (RCC)...")
    qrc = ROOT / "resources/resources.qrc"
    if not qrc.exists():
        print("  No resources.qrc found, skipping")
        return
    run([str(RCC), str(qrc), "-o", str(GEN_DIR / "qrc_resources.cpp")])


def step_premake():
    print("\n[3/4] Regenerating project files (premake5)...")
    run(["premake5", "vs2026"], cwd=str(ROOT))


def step_msbuild():
    print("\n[4/4] Building Release (MSBuild)...")
    env = get_vs_env()
    run(
        [str(MSBUILD), str(VCXPROJ),
         "/p:Configuration=Release", "/p:Platform=x64", "/v:minimal"],
        env=env,
    )
    print(f"\n  Output: {ROOT / 'bin/Release/CheckDown.exe'}")


def step_installer():
    print("\n[+] Building installer (NSIS)...")
    exe = ROOT / "bin/Release/CheckDown.exe"
    if not exe.exists():
        print(f"ERROR: {exe} not found. Run the Release build first.")
        sys.exit(1)
    run([str(NSIS), str(NSI)], cwd=str(ROOT / "installer"))
    print(f"\n  Output: {ROOT / 'installer/CheckDown-Setup.exe'}")


def step_update_ytdlp():
    """Download latest yt-dlp.exe if newer than the bundled copy."""
    print("\n[+] Checking yt-dlp for updates...")
    ytdlp_dir = ROOT / "vendor/yt-dlp"
    ytdlp_exe = ytdlp_dir / "yt-dlp.exe"

    # Get latest release tag from GitHub API
    try:
        req = urllib.request.Request(
            "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest",
            headers={"Accept": "application/vnd.github.v3+json", "User-Agent": "CheckDown-Build"},
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            release = json.loads(resp.read())
        latest_tag = release["tag_name"]
    except Exception as e:
        print(f"  Could not check latest version: {e}")
        return

    # Check current version (if exists)
    current_tag = None
    if ytdlp_exe.exists():
        try:
            r = subprocess.run(
                [str(ytdlp_exe), "--version"],
                capture_output=True, text=True, timeout=10,
            )
            if r.returncode == 0:
                current_tag = r.stdout.strip()
        except Exception:
            pass

    if current_tag and current_tag == latest_tag:
        print(f"  yt-dlp is up to date ({current_tag})")
        return

    print(f"  Updating yt-dlp: {current_tag or '(not found)'} -> {latest_tag}")
    ytdlp_dir.mkdir(parents=True, exist_ok=True)
    url = f"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
    try:
        urllib.request.urlretrieve(url, str(ytdlp_exe))
        size_mb = ytdlp_exe.stat().st_size / (1024 * 1024)
        print(f"  Downloaded yt-dlp {latest_tag} ({size_mb:.1f} MB)")
    except Exception as e:
        print(f"  WARNING: Failed to download yt-dlp: {e}")


def step_pack_extension():
    print("\n[+] Packing Chrome extension...")
    ext_dir = ROOT / "extension"
    if not ext_dir.exists():
        print(f"ERROR: extension directory not found at {ext_dir}")
        sys.exit(1)

    out_zip = ROOT / "installer/CheckDown-Extension.zip"
    out_zip.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        for file in sorted(ext_dir.rglob("*")):
            if file.is_file():
                zf.write(file, file.relative_to(ext_dir))

    size_kb = out_zip.stat().st_size // 1024
    print(f"\n  Output: {out_zip}  ({size_kb} KB)")


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    do_installer  = "--installer" in sys.argv or "--all" in sys.argv
    do_extension  = "--all" in sys.argv
    do_ytdlp     = "--update-ytdlp" in sys.argv or "--all" in sys.argv

    print("=== CheckDown Build ===")

    if do_ytdlp:
        step_update_ytdlp()

    step_moc()
    step_rcc()
    step_premake()
    step_msbuild()

    if do_installer:
        step_installer()

    if do_extension:
        step_pack_extension()

    print("\n=== Done ===")
