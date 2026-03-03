// CheckDown Chrome Extension Popup

const statusDot = document.getElementById("statusDot");
const statusText = document.getElementById("statusText");
const autoInterceptToggle = document.getElementById("autoIntercept");
const urlInput = document.getElementById("urlInput");
const addBtn = document.getElementById("addBtn");

// ---------------------------------------------------------------------------
// Connection Status
// ---------------------------------------------------------------------------
async function updateConnectionStatus() {
  try {
    const response = await chrome.runtime.sendMessage({ type: "ping" });
    if (response && response.connected) {
      statusDot.className = "status-dot connected";
      statusText.textContent = "Connected to CheckDown";
      addBtn.disabled = false;
    } else {
      setDisconnected();
    }
  } catch {
    setDisconnected();
  }
}

function setDisconnected() {
  statusDot.className = "status-dot disconnected";
  statusText.textContent = "CheckDown is not running";
  addBtn.disabled = true;
}

// ---------------------------------------------------------------------------
// Auto-Intercept Toggle
// ---------------------------------------------------------------------------
chrome.storage.sync.get({ autoIntercept: true }, (settings) => {
  autoInterceptToggle.checked = settings.autoIntercept;
});

autoInterceptToggle.addEventListener("change", () => {
  chrome.storage.sync.set({ autoIntercept: autoInterceptToggle.checked });
});

// ---------------------------------------------------------------------------
// Add URL
// ---------------------------------------------------------------------------
addBtn.addEventListener("click", () => {
  const url = urlInput.value.trim();
  if (!url) {
    urlInput.focus();
    return;
  }

  // Basic URL validation
  try {
    new URL(url);
  } catch {
    urlInput.style.borderColor = "#ef4444";
    setTimeout(() => {
      urlInput.style.borderColor = "";
    }, 1500);
    return;
  }

  addBtn.disabled = true;
  addBtn.textContent = "Sending...";

  chrome.runtime.sendMessage(
    { type: "addUrl", url: url },
    (response) => {
      addBtn.textContent = "Sent!";
      urlInput.value = "";

      setTimeout(() => {
        addBtn.disabled = false;
        addBtn.textContent = "Download with CheckDown";
      }, 1500);
    }
  );
});

// Enter key triggers download
urlInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") {
    addBtn.click();
  }
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
updateConnectionStatus();

// Refresh connection status every 5 seconds while popup is open
setInterval(updateConnectionStatus, 5000);
