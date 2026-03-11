# CheckDown

A fast, multi-segment download manager for Windows with a Chrome/Edge extension for browser integration.

## Features

- **Multi-segment downloading** — splits files into parallel segments for faster speeds
- **Browser extension** — intercepts downloads automatically; detects embedded media (video/audio) on pages including YouTube, TikTok, and Instagram
- **Download organization** — automatically sorts files into subfolders (Videos, Audio, Images, Documents, Archives, Programs)
- **Resume support** — paused or interrupted downloads pick up where they left off
- **System tray** — runs quietly in the background; show/hide with a click
- **Native Messaging** — extension communicates with the app via Chrome's secure native messaging API, no open ports

## Screenshots

> Coming soon.

## Installation

1. Download `CheckDown-Setup.exe` from the [latest release](https://github.com/mKonic/CheckDown/releases/latest)
2. Run the installer — it registers the native messaging host automatically (no admin required)
3. Load the Chrome extension:
   - Open `chrome://extensions`
   - Enable **Developer mode**
   - Click **Load unpacked** → select the `extension/` folder inside the installation directory
4. Launch CheckDown — the extension status dot will turn green

> The extension ID will be `bilkcagfjmdbanpopmgpeomknejeiand` regardless of where you load it from (the manifest includes a stable key).

## Building from Source

### Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| Visual Studio / Build Tools | 2026 (v145) | C++23 required |
| Qt | 6.8.3 (MSVC 2022 x64) | via aqtinstall or Qt Installer |
| libcurl | 8.x (static) | with SSL |
| Premake5 | 5.0.0-beta8+ | in PATH |
| NSIS | 3.x | for installer only |

Set environment variables or edit `premake5.lua`:
```
QT_DIR   = C:/path/to/qt/6.8.3/msvc2022_64
CURL_DIR = C:/path/to/curl-install
```

### Build

```bat
premake5 vs2026
scripts\do_build.bat
```

### Build installer

```bat
scripts\do_installer.bat
```

Output: `installer/CheckDown-Setup.exe`

## Downloading from YouTube, TikTok, and Instagram

CheckDown bundles **[yt-dlp](https://github.com/yt-dlp/yt-dlp)** (`vendor/yt-dlp/yt-dlp.exe`) to handle downloads from these platforms. When you're on a YouTube, TikTok, or Instagram page, the extension popup shows **↓ Download** (and **↓ Playlist** on YouTube playlist pages) buttons that send the page URL to CheckDown, which then invokes yt-dlp internally. Progress (percent, speed, ETA, filename) is streamed back to the download table in real time.

### Cookie support

Some content requires authentication (age-gated videos, private accounts). The extension popup's **Downloads** tab shows a **Send cookies for <site>** toggle for supported platforms. When enabled, the extension forwards the current session cookies for that site along with the download request so yt-dlp (or libcurl for regular downloads) can authenticate.


## Architecture

```
CheckDown.exe (UI app)
  ├── DownloadManager      task queue, concurrency, state persistence
  ├── DownloadTask         per-download state machine (regular HTTP)
  │   └── Segment (×N)    parallel byte-range workers via libcurl
  ├── YtdlpTask            yt-dlp subprocess wrapper (YouTube/TikTok/Instagram)
  ├── PipeServer           \\.\pipe\CheckDown — NMH bridge endpoint
  └── MainWindow           Qt6 UI, tray icon

CheckDown.exe (NMH bridge mode, spawned by Chrome)
  └── stdin/stdout ↔ \\.\pipe\CheckDown relay

Chrome Extension
  ├── background/service-worker.js   sendNativeMessage, webRequest media detection
  ├── content/interceptor.js         MAIN world — YT/TikTok/IG globals + fetch/XHR hooks
  ├── content/content.js             isolated world — DOM scan, postMessage relay
  └── popup/                         two-tab UI (Downloads + Media)
```

## Project Structure

```
src/
  core/        DownloadManager, DownloadTask, YtdlpTask, Segment, HttpClient, Logger, Types, Version
  server/      PipeServer (named pipe IPC)
  ui/          MainWindow, AddDownloadDialog, SettingsDialog, DownloadTableModel, TrayManager
extension/
  background/  service-worker.js
  content/     interceptor.js (MAIN world), content.js (isolated)
  popup/       popup.html/js/css
  icons/
installer/     checkdown.nsi
resources/     app icons (.png, .ico)
scripts/       build helpers
```

## License

MIT
