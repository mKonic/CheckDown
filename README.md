# CheckDown

A fast, multi-segment download manager for Windows with a Chrome/Edge extension for browser integration.

## Features

- Multi-segment parallel downloading
- Pause, resume, and cancel downloads
- Automatic file categorization (Videos, Audio, Images, Documents, Archives, Programs)
- Browser extension with automatic download interception
- yt-dlp integration (YouTube, TikTok, Instagram)
- Cookie forwarding for authenticated downloads
- Drag & drop and clipboard paste (Ctrl+V)
- Dark theme (Catppuccin Mocha)

## Installation

1. Download `CheckDown-Setup.exe` from the [latest release](https://github.com/mKonic/CheckDown/releases/latest)
2. Run the installer — it registers the native messaging host automatically (no admin required)
3. Load the Chrome extension:
   - Open `chrome://extensions`
   - Enable **Developer mode**
   - Click **Load unpacked** → select the `extension/` folder inside the install directory
4. Launch CheckDown — the extension status dot will turn green

## Building from Source

### Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| Visual Studio / Build Tools | 2026 (v145) | C++23 required |
| Qt | 6.8.3 (MSVC 2022 x64) | via aqtinstall or Qt Installer |
| libcurl | 8.x (static) | with SSL |
| Premake5 | 5.0.0-beta8+ | in PATH |
| NSIS | 3.x | for installer only |

### Build

```bash
python scripts/build.py --all
```

This runs MOC/RCC generation, Premake, MSBuild (Release), NSIS installer, and extension packaging in one step.

To build individual steps:
```bash
python scripts/build.py          # compile only
python scripts/build.py --installer  # compile + installer
```

Output:
- `bin/Release/CheckDown.exe`
- `installer/CheckDown-Setup.exe`
- `installer/CheckDown-Extension.zip`

## yt-dlp

CheckDown bundles [yt-dlp](https://github.com/yt-dlp/yt-dlp) for YouTube, TikTok, and Instagram downloads. The extension popup shows download buttons on supported pages. Progress is streamed back to the UI in real time.

### Cookies

Some content requires authentication (age-gated videos, private accounts). The extension popup has a **Send cookies** toggle for supported sites. When enabled, session cookies are forwarded with proper domain matching so yt-dlp or libcurl can authenticate.

## License

MIT
