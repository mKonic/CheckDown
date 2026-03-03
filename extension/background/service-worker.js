// CheckDown - Chrome Extension Service Worker
// Intercepts downloads and forwards them to CheckDown app via local HTTP server.

const CHECKDOWN_API = "http://localhost:18693/api";

// File extensions to intercept (common downloadable file types)
const INTERCEPT_EXTENSIONS = new Set([
  // Archives
  "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
  // Executables / installers
  "exe", "msi", "dmg", "deb", "rpm", "appimage",
  // Media
  "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm",
  "mp3", "flac", "wav", "aac", "ogg", "wma",
  // Images (large)
  "iso", "img",
  // Documents (large)
  "pdf", "docx", "xlsx", "pptx",
  // Other
  "torrent", "apk",
]);

// Minimum file size to intercept (bytes) - skip tiny files
const MIN_INTERCEPT_SIZE = 1024 * 1024; // 1 MB

// ---------------------------------------------------------------------------
// Context Menu
// ---------------------------------------------------------------------------
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "checkdown-download-link",
    title: "Download with CheckDown",
    contexts: ["link", "image", "video", "audio"],
  });
});

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (info.menuItemId === "checkdown-download-link") {
    const url = info.linkUrl || info.srcUrl;
    if (url) {
      sendToCheckDown(url);
    }
  }
});

// ---------------------------------------------------------------------------
// Download Interception
// ---------------------------------------------------------------------------
chrome.downloads.onDeterminingFilename.addListener((downloadItem, suggest) => {
  chrome.storage.sync.get({ autoIntercept: true }, (settings) => {
    if (!settings.autoIntercept) {
      suggest();
      return;
    }

    // Check file extension
    const fileName = downloadItem.filename || "";
    const ext = fileName.split(".").pop().toLowerCase();
    const shouldInterceptByExt = INTERCEPT_EXTENSIONS.has(ext);

    // Check file size (if known)
    const fileSize = downloadItem.fileSize || 0;
    const shouldInterceptBySize = fileSize >= MIN_INTERCEPT_SIZE;

    // Intercept if extension matches OR file is large enough
    if (shouldInterceptByExt || shouldInterceptBySize) {
      // Cancel Chrome's download
      chrome.downloads.cancel(downloadItem.id);

      // Send to CheckDown
      sendToCheckDown(downloadItem.url, fileName, fileSize);

      // Don't suggest a filename (download is cancelled)
      return;
    }

    // Let Chrome handle small/unmatched files
    suggest();
  });
});

// ---------------------------------------------------------------------------
// API Communication
// ---------------------------------------------------------------------------
async function sendToCheckDown(url, fileName = "", fileSize = -1) {
  try {
    const response = await fetch(`${CHECKDOWN_API}/download`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        url: url,
        fileName: fileName,
        fileSize: fileSize,
        segments: 8,
      }),
    });

    if (response.ok) {
      // Show success notification
      chrome.notifications.create({
        type: "basic",
        iconUrl: "icons/icon128.png",
        title: "CheckDown",
        message: `Download sent: ${fileName || url.split("/").pop()}`,
      });
    } else {
      throw new Error(`Server responded with ${response.status}`);
    }
  } catch (error) {
    console.error("CheckDown: Failed to send download", error);
    chrome.notifications.create({
      type: "basic",
      iconUrl: "icons/icon128.png",
      title: "CheckDown - Error",
      message:
        "Could not connect to CheckDown. Make sure the app is running.",
    });
  }
}

// ---------------------------------------------------------------------------
// Health Check (used by popup)
// ---------------------------------------------------------------------------
async function checkConnection() {
  try {
    const response = await fetch(`${CHECKDOWN_API}/ping`, {
      signal: AbortSignal.timeout(2000),
    });
    return response.ok;
  } catch {
    return false;
  }
}

// Listen for messages from popup
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message.type === "ping") {
    checkConnection().then((connected) => {
      sendResponse({ connected });
    });
    return true; // async response
  }

  if (message.type === "addUrl") {
    sendToCheckDown(message.url, message.fileName || "");
    sendResponse({ ok: true });
    return false;
  }
});
