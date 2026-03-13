// CheckDown Chrome Extension Popup

// ---- Elements ----
const statusDot   = document.getElementById("statusDot");
const statusText  = document.getElementById("statusText");
const activeBadge = document.getElementById("activeBadge");
const urlInput    = document.getElementById("urlInput");
const addBtn      = document.getElementById("addBtn");
const mediaList   = document.getElementById("mediaList");
const mediaEmpty  = document.getElementById("mediaEmpty");
const autoInterceptToggle = document.getElementById("autoIntercept");

let currentTabUrl = null;

// ---------------------------------------------------------------------------
// Platform / yt-dlp helpers
// ---------------------------------------------------------------------------
function getYtdlpPlatform(url) {
  if (!url) return null;
  try {
    const h = new URL(url).hostname;
    if (h.includes("youtube.com") || h.includes("youtu.be")) return "youtube";
    if (h.includes("instagram.com"))                          return "instagram";
    if (h.includes("tiktok.com"))                            return "tiktok";
    if (h.includes("twitter.com") || h.includes("x.com"))   return "twitter";
    if (h.includes("reddit.com"))                            return "reddit";
    if (h.includes("vimeo.com"))                             return "vimeo";
    if (h.includes("twitch.tv"))                             return "twitch";
  } catch {}
  return null;
}

function isYouTubePlaylist(url) {
  try { return new URL(url).searchParams.has("list"); } catch { return false; }
}

function stripPlaylistParam(url) {
  try {
    const u = new URL(url);
    u.searchParams.delete("list");
    u.searchParams.delete("index");
    return u.toString();
  } catch { return url; }
}

function sendYtdlp(url, isPlaylist) {
  chrome.runtime.sendMessage({ type: "ytdlp", url, isPlaylist });
}

// ---------------------------------------------------------------------------
// Tab switching
// ---------------------------------------------------------------------------
document.querySelectorAll(".tab-btn").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".tab-btn").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".tab-pane").forEach((p) => p.classList.add("hidden"));
    btn.classList.add("active");
    document.getElementById("tab-" + btn.dataset.tab).classList.remove("hidden");

    if (btn.dataset.tab === "media") renderVideoTab();
  });
});

// ---------------------------------------------------------------------------
// Connection + active downloads
// ---------------------------------------------------------------------------
async function updateConnectionStatus() {
  try {
    const pingResp = await chrome.runtime.sendMessage({ type: "ping" });
    if (pingResp?.connected) {
      statusDot.className = "status-dot connected";
      statusText.textContent = "Connected to CheckDown";
      addBtn.disabled = false;

      const dlResp = await chrome.runtime.sendMessage({ type: "getDownloads" });
      if (dlResp?.data?.downloads) {
        const active = dlResp.data.downloads.filter((d) => d.state === "Downloading").length;
        if (active > 0) {
          activeBadge.textContent = `${active} active`;
          activeBadge.style.display = "inline-block";
        } else {
          activeBadge.style.display = "none";
        }
      }
    } else {
      setDisconnected();
    }
  } catch {
    setDisconnected();
  }
}

function setDisconnected() {
  statusDot.className = "status-dot disconnected";
  statusText.textContent = "Please launch CheckDown";
  addBtn.disabled = true;
  activeBadge.style.display = "none";
}

// ---------------------------------------------------------------------------
// Auto-Intercept Toggle
// ---------------------------------------------------------------------------
chrome.storage.sync.get({ autoIntercept: true }, (s) => {
  autoInterceptToggle.checked = s.autoIntercept;
});
autoInterceptToggle.addEventListener("change", () => {
  chrome.storage.sync.set({ autoIntercept: autoInterceptToggle.checked });
});

// ---------------------------------------------------------------------------
// Cookie-per-site Toggle
// ---------------------------------------------------------------------------
const cookieToggleRow   = document.getElementById("cookieToggleRow");
const cookieToggleLabel = document.getElementById("cookieToggleLabel");
const cookieSiteToggle  = document.getElementById("cookieSiteToggle");

function initCookieToggle(hostname) {
  if (!hostname || hostname.startsWith("chrome")) return;
  cookieToggleLabel.textContent = `Send cookies for ${hostname}`;
  cookieToggleRow.classList.remove("hidden");

  chrome.runtime.sendMessage({ type: "getCookieSite", hostname }, (resp) => {
    cookieSiteToggle.checked = resp?.enabled || false;
  });
}

cookieSiteToggle.addEventListener("change", () => {
  if (!currentTabUrl) return;
  try {
    const hostname = new URL(currentTabUrl).hostname;
    chrome.runtime.sendMessage({
      type: "setCookieSite", hostname, enabled: cookieSiteToggle.checked,
    });
  } catch {}
});

// ---------------------------------------------------------------------------
// Add URL
// ---------------------------------------------------------------------------
addBtn.addEventListener("click", () => {
  const url = urlInput.value.trim();
  if (!url) { urlInput.focus(); return; }

  try { new URL(url); } catch {
    urlInput.style.borderColor = "#ef4444";
    setTimeout(() => { urlInput.style.borderColor = ""; }, 1500);
    return;
  }

  const orig = addBtn.textContent;
  addBtn.disabled = true;
  addBtn.textContent = "Sending...";

  chrome.runtime.sendMessage({ type: "addUrl", url }, () => {
    addBtn.textContent = "Sent!";
    urlInput.value = "";
    setTimeout(() => {
      addBtn.disabled = false;
      addBtn.textContent = orig;
      updateConnectionStatus();
    }, 1500);
  });
});

urlInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") addBtn.click();
});

// ---------------------------------------------------------------------------
// Video Tab — yt-dlp platform card
// ---------------------------------------------------------------------------
function renderVideoTab() {
  const platform = getYtdlpPlatform(currentTabUrl);

  // Remove existing card
  mediaList.querySelectorAll(".platform-card").forEach((el) => el.remove());

  if (!platform) {
    mediaEmpty.style.display = "block";
    return;
  }

  mediaEmpty.style.display = "none";

  const icons = { youtube: "▶", instagram: "📷", tiktok: "♪", twitter: "𝕏", reddit: "🔴", vimeo: "▶", twitch: "🟣" };
  const names = { youtube: "YouTube", instagram: "Instagram", tiktok: "TikTok", twitter: "Twitter / X", reddit: "Reddit", vimeo: "Vimeo", twitch: "Twitch" };

  const card = document.createElement("div");
  card.className = "platform-card";

  const iconEl = document.createElement("div");
  iconEl.className = "platform-icon";
  iconEl.textContent = icons[platform];

  const infoEl = document.createElement("div");
  infoEl.className = "platform-info";
  infoEl.innerHTML = `<div class="platform-name">${names[platform]}</div>`
                   + `<div class="platform-sub">Download with yt-dlp</div>`;

  const btnsEl = document.createElement("div");
  btnsEl.className = "platform-btns";

  if (platform === "youtube" && isYouTubePlaylist(currentTabUrl)) {
    const btnVid = document.createElement("button");
    btnVid.className = "btn-ytdlp";
    btnVid.title = "Download this video only";
    btnVid.textContent = "↓ Video";
    btnVid.addEventListener("click", () => sendYtdlp(stripPlaylistParam(currentTabUrl), false));

    const btnPl = document.createElement("button");
    btnPl.className = "btn-ytdlp";
    btnPl.title = "Download full playlist";
    btnPl.textContent = "↓ Playlist";
    btnPl.addEventListener("click", () => sendYtdlp(currentTabUrl, true));

    btnsEl.appendChild(btnVid);
    btnsEl.appendChild(btnPl);
  } else {
    const btn = document.createElement("button");
    btn.className = "btn-ytdlp";
    btn.title = "Download with yt-dlp";
    btn.textContent = "↓ Download";
    btn.addEventListener("click", () => sendYtdlp(currentTabUrl, false));
    btnsEl.appendChild(btn);
  }

  card.appendChild(iconEl);
  card.appendChild(infoEl);
  card.appendChild(btnsEl);
  mediaList.insertBefore(card, mediaEmpty);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
(async () => {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  currentTabUrl = tab?.url ?? null;

  updateConnectionStatus();

  if (currentTabUrl) {
    try {
      initCookieToggle(new URL(currentTabUrl).hostname);
    } catch {}
  }

  // Auto-switch to Video tab on yt-dlp platforms
  if (getYtdlpPlatform(currentTabUrl)) {
    document.querySelector('[data-tab="media"]').click();
  }
})();

setInterval(updateConnectionStatus, 5000);
