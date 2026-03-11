// CheckDown - Chrome Extension Service Worker

const NMH_HOST = "com.checkdown.app";

// File extensions to auto-intercept
const INTERCEPT_EXTENSIONS = new Set([
  "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "zst",
  "exe", "msi", "dmg", "deb", "rpm", "appimage", "pkg",
  "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v",
  "mp3", "flac", "wav", "aac", "ogg", "wma", "opus", "m4a",
  "iso", "img",
  "pdf", "docx", "xlsx", "pptx",
  "apk", "ipa",
]);

// ---------------------------------------------------------------------------
// Native Messaging
// ---------------------------------------------------------------------------
function nmhSend(message) {
  return new Promise((resolve, reject) => {
    try {
      chrome.runtime.sendNativeMessage(NMH_HOST, { ...message, id: 1 }, (resp) => {
        if (chrome.runtime.lastError) {
          reject(new Error(chrome.runtime.lastError.message));
        } else {
          resolve(resp ?? {});
        }
      });
    } catch (e) {
      reject(e);
    }
  });
}

async function checkConnection() {
  try {
    const r = await nmhSend({ type: "ping" });
    return r?.connected === true;
  } catch { return false; }
}

async function getDownloadList() {
  try {
    const r = await nmhSend({ type: "getDownloads" });
    if (!r || r.connected === false) return null;
    return r;
  } catch { return null; }
}

// ---------------------------------------------------------------------------
// Cookie helpers
// ---------------------------------------------------------------------------
async function getCookiesForUrl(url) {
  try {
    const hostname = new URL(url).hostname;
    const { cookieSites = {} } = await chrome.storage.local.get("cookieSites");
    if (!cookieSites[hostname]) return [];
    const cookies = await chrome.cookies.getAll({ domain: hostname });
    return cookies.map((c) => ({
      name:           c.name,
      value:          c.value,
      domain:         c.domain,
      path:           c.path,
      secure:         c.secure,
      httpOnly:       c.httpOnly,
      expirationDate: c.expirationDate || 0,
    }));
  } catch { return []; }
}

function getFilenameFromUrl(url) {
  try {
    const path = new URL(url).pathname;
    return decodeURIComponent(path.split("/").pop() || "") || "file";
  } catch { return "file"; }
}

// ---------------------------------------------------------------------------
// Send to app
// ---------------------------------------------------------------------------
async function sendToCheckDown(url, fileName = "", fileSize = -1) {
  const [settings, cookies] = await Promise.all([
    chrome.storage.sync.get({ defaultSegments: 8 }),
    getCookiesForUrl(url),
  ]);
  try {
    const r = await nmhSend({
      type: "addUrl",
      url,
      fileName,
      fileSize,
      segments: settings.defaultSegments,
      cookies,
    });
    if (r?.connected === false || r?.error) throw new Error(r.error || "Not connected");

    chrome.notifications.create({
      type: "basic", iconUrl: "icons/icon128.png",
      title: "CheckDown",
      message: `Downloading: ${fileName || getFilenameFromUrl(url)}`,
    });
    setTimeout(updateBadge, 800);
  } catch {
    chrome.notifications.create({
      type: "basic", iconUrl: "icons/icon128.png",
      title: "CheckDown — Not Running",
      message: "Please launch CheckDown first, then try again.",
    });
  }
}

async function sendToYtdlp(pageUrl, isPlaylist) {
  const cookies = await getCookiesForUrl(pageUrl);
  try {
    const r = await nmhSend({ type: "ytdlp", url: pageUrl, isPlaylist, cookies });
    if (r?.connected === false || r?.error) throw new Error(r.error || "Not connected");
    chrome.notifications.create({
      type: "basic", iconUrl: "icons/icon128.png",
      title: "CheckDown",
      message: isPlaylist ? "yt-dlp: playlist queued" : "yt-dlp: video queued",
    });
    setTimeout(updateBadge, 800);
  } catch {
    chrome.notifications.create({
      type: "basic", iconUrl: "icons/icon128.png",
      title: "CheckDown — Not Running",
      message: "Please launch CheckDown first, then try again.",
    });
  }
}

// ---------------------------------------------------------------------------
// Badge
// ---------------------------------------------------------------------------
async function updateBadge() {
  try {
    const data = await getDownloadList();
    const active = (data?.downloads || []).filter((d) => d.state === "Downloading").length;
    if (active > 0) {
      chrome.action.setBadgeText({ text: String(active) });
      chrome.action.setBadgeBackgroundColor({ color: "#3399ff" });
    } else {
      chrome.action.setBadgeText({ text: "" });
    }
  } catch {
    chrome.action.setBadgeText({ text: "" });
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "checkdown-download-link",
    title: "Download with CheckDown",
    contexts: ["link", "image", "video", "audio"],
  });
  chrome.alarms.create("badgeUpdate", { periodInMinutes: 1 });
});

chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === "badgeUpdate") updateBadge();
});

// ---------------------------------------------------------------------------
// Context Menu
// ---------------------------------------------------------------------------
chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId === "checkdown-download-link") {
    const url = info.linkUrl || info.srcUrl;
    if (url) sendToCheckDown(url);
  }
});

// ---------------------------------------------------------------------------
// Download Interception
// ---------------------------------------------------------------------------
chrome.downloads.onDeterminingFilename.addListener((downloadItem, suggest) => {
  chrome.storage.sync.get(
    { autoIntercept: true, minInterceptSize: 1024 * 1024 },
    (settings) => {
      if (!settings.autoIntercept) { suggest(); return; }

      const fileName = downloadItem.filename || "";
      const ext    = fileName.split(".").pop().toLowerCase();
      const byExt  = INTERCEPT_EXTENSIONS.has(ext);
      const bySize = (downloadItem.fileSize || 0) >= settings.minInterceptSize;

      if (byExt || bySize) {
        chrome.downloads.cancel(downloadItem.id, () => {
          chrome.downloads.erase({ id: downloadItem.id });
        });
        sendToCheckDown(downloadItem.url, fileName, downloadItem.fileSize || -1);
        return;
      }
      suggest();
    }
  );
});

// ---------------------------------------------------------------------------
// Messages from popup
// ---------------------------------------------------------------------------
chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {

  if (message.type === "ping") {
    checkConnection().then((connected) => sendResponse({ connected }));
    return true;
  }

  if (message.type === "getDownloads") {
    getDownloadList().then((data) => sendResponse({ data }));
    return true;
  }

  if (message.type === "addUrl") {
    sendToCheckDown(message.url, message.fileName || "")
      .then(() => sendResponse({ ok: true }))
      .catch(() => sendResponse({ ok: false }));
    return true;
  }

  if (message.type === "ytdlp") {
    sendToYtdlp(message.url, message.isPlaylist || false)
      .then(() => sendResponse({ ok: true }))
      .catch(() => sendResponse({ ok: false }));
    return true;
  }

  if (message.type === "setCookieSite") {
    chrome.storage.local.get("cookieSites", ({ cookieSites = {} }) => {
      cookieSites[message.hostname] = message.enabled;
      chrome.storage.local.set({ cookieSites });
      sendResponse({ ok: true });
    });
    return true;
  }

  if (message.type === "getCookieSite") {
    chrome.storage.local.get("cookieSites", ({ cookieSites = {} }) => {
      sendResponse({ enabled: !!cookieSites[message.hostname] });
    });
    return true;
  }
});
