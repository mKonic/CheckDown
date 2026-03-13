#include "MainWindow.h"
#include "AddDownloadDialog.h"
#include "TrayManager.h"
#include "SettingsDialog.h"
#include "../server/PipeServer.h"
#include "../core/Logger.h"
#include "../core/Version.h"

#include <QCloseEvent>
#include <QResizeEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStyle>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QMenu>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <filesystem>

namespace checkdown {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, "CheckDown", "CheckDown")
{
    setWindowTitle("CheckDown");
    resize(1020, 560);

    // State / log directory
    auto configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    std::filesystem::path stateDir = configDir.toStdString();

    // Init logger before anything else so all operations are captured
    Logger::instance().init(stateDir / "checkdown.log");
    LOG_INFO("=== MainWindow initialising ===");

    // Load settings early so m_defaultSegments and maxConcurrent are ready
    loadSettings();

    // Apply modern stylesheet
    applyStylesheet();

    // Enable drag & drop
    setAcceptDrops(true);

    // Download manager with progress bridge
    m_manager = std::make_unique<DownloadManager>(
        stateDir,
        [this](const TaskProgress& p) {
            // This fires on worker threads — emit signal for queued delivery
            emit downloadProgressReceived(p);
        }
    );
    m_manager->setMaxConcurrent(m_settings.value("maxConcurrent", kDefaultMaxConcurrent).toInt());

    // Cross-thread signal -> slot
    connect(this, &MainWindow::downloadProgressReceived,
            this, &MainWindow::onDownloadProgress,
            Qt::QueuedConnection);

    // Table model + sortable proxy
    m_model      = new DownloadTableModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_delegate   = new ProgressDelegate(this);

    m_tableView = new QTableView;
    m_tableView->setModel(m_proxyModel);
    m_tableView->setItemDelegateForColumn(DownloadTableModel::ColProgress, m_delegate);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setShowGrid(false);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(DownloadTableModel::ColId, Qt::DescendingOrder);
    m_tableView->verticalHeader()->hide();
    m_tableView->verticalHeader()->setDefaultSectionSize(30);
    m_tableView->horizontalHeader()->setStretchLastSection(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(
        DownloadTableModel::ColFileName, QHeaderView::Stretch);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->setColumnWidth(DownloadTableModel::ColId,        40);
    m_tableView->setColumnWidth(DownloadTableModel::ColSize,      90);
    m_tableView->setColumnWidth(DownloadTableModel::ColProgress, 150);
    m_tableView->setColumnWidth(DownloadTableModel::ColSpeed,    100);
    m_tableView->setColumnWidth(DownloadTableModel::ColEta,       72);
    m_tableView->setColumnWidth(DownloadTableModel::ColStatus,    90);
    m_tableView->setColumnHidden(DownloadTableModel::ColUrl, true);
    m_tableView->setFrameShape(QFrame::NoFrame);

    // Empty state overlay
    m_emptyLabel = new QLabel(m_tableView);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setText(
        "<div style='color: #585b70; font-size: 14px; line-height: 2;'>"
        "<span style='font-size: 28px;'>No downloads yet</span><br>"
        "Click <b>Add URL</b> or drag a link here to get started<br>"
        "<span style='font-size: 12px; color: #45475a;'>Ctrl+N to add  |  Ctrl+V to paste URL</span>"
        "</div>"
    );
    m_emptyLabel->setTextFormat(Qt::RichText);

    // Central widget: stack table + empty label
    setCentralWidget(m_tableView);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_tableView, &QTableView::doubleClicked,
            this, &MainWindow::onTableDoubleClicked);
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &MainWindow::onTableContextMenu);

    createActions();
    createToolBar();
    createStatusBar();

    // System tray
    setupTray();

    // Named-pipe server + NMH registration for Chrome extension
    setupPipeServer();
    registerNativeMessagingHost();

    // Load saved download state
    m_manager->loadState();
    m_model->setDownloads(m_manager->allDownloads());
    updateButtonStates();
    updateWindowTitle();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Actions & Toolbar
// ---------------------------------------------------------------------------
void MainWindow::createActions() {
    auto icon = [](QStyle::StandardPixmap sp) {
        return QApplication::style()->standardIcon(sp);
    };

    m_addAction = new QAction(icon(QStyle::SP_FileDialogNewFolder), "Add URL", this);
    m_addAction->setShortcut(QKeySequence("Ctrl+N"));
    m_addAction->setToolTip("Add a new download (Ctrl+N)");
    connect(m_addAction, &QAction::triggered, this, &MainWindow::onAddDownload);

    m_pauseAction = new QAction(icon(QStyle::SP_MediaPause), "Pause / Resume", this);
    m_pauseAction->setEnabled(false);
    m_pauseAction->setToolTip("Pause or resume the selected download");
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPauseResume);

    m_cancelAction = new QAction(icon(QStyle::SP_BrowserStop), "Cancel", this);
    m_cancelAction->setEnabled(false);
    m_cancelAction->setToolTip("Cancel the selected download");
    connect(m_cancelAction, &QAction::triggered, this, &MainWindow::onCancel);

    m_removeAction = new QAction(icon(QStyle::SP_TrashIcon), "Remove", this);
    m_removeAction->setEnabled(false);
    m_removeAction->setShortcut(QKeySequence::Delete);
    m_removeAction->setToolTip("Remove the selected download from the list (Del)");
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::onRemove);

    m_clearFinishedAction = new QAction(icon(QStyle::SP_DialogResetButton), "Clear Finished", this);
    m_clearFinishedAction->setToolTip("Remove all completed, cancelled, and failed downloads");
    connect(m_clearFinishedAction, &QAction::triggered, this, &MainWindow::onClearFinished);

    m_settingsAction = new QAction(icon(QStyle::SP_ComputerIcon), "Settings", this);
    m_settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    m_settingsAction->setToolTip("Open settings (Ctrl+,)");
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

    // Ctrl+V: paste URL from clipboard and start downloading
    auto* pasteAction = new QAction(this);
    pasteAction->setShortcut(QKeySequence("Ctrl+V"));
    connect(pasteAction, &QAction::triggered, this, [this] {
        QString clip = QApplication::clipboard()->text().trimmed();
        if (!clip.isEmpty() && (clip.startsWith("http://") || clip.startsWith("https://"))) {
            addDownloadFromExternal(clip.toStdString());
        }
    });
    addAction(pasteAction);
}

void MainWindow::createToolBar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(18, 18));
    tb->addAction(m_addAction);
    tb->addSeparator();
    tb->addAction(m_pauseAction);
    tb->addAction(m_cancelAction);
    tb->addAction(m_removeAction);
    tb->addSeparator();
    tb->addAction(m_clearFinishedAction);

    // Spacer to push settings to the right
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addAction(m_settingsAction);
}

void MainWindow::createStatusBar() {
    m_statusLabel = new QLabel("Ready");
    statusBar()->addPermanentWidget(m_statusLabel);
}

// ---------------------------------------------------------------------------
// System Tray
// ---------------------------------------------------------------------------
void MainWindow::setupTray() {
    m_trayManager = new TrayManager(this, this);

    connect(m_trayManager, &TrayManager::showWindowRequested,
            this, &MainWindow::onShowWindow);
    connect(m_trayManager, &TrayManager::addUrlRequested,
            this, &MainWindow::onAddDownload);
    connect(m_trayManager, &TrayManager::settingsRequested,
            this, &MainWindow::onSettings);
    connect(m_trayManager, &TrayManager::exitRequested,
            this, &MainWindow::onExitApp);
}

// ---------------------------------------------------------------------------
// Named-pipe server (Chrome extension via NMH bridge)
// ---------------------------------------------------------------------------
void MainWindow::setupPipeServer() {
    m_pipeServer = new PipeServer(this);

    connect(m_pipeServer, &PipeServer::downloadRequested,
            this, [this](const QString& url, const QString& fileName,
                         int64_t fileSize, int segments, const QString& cookies) {
                addDownloadFromExternal(url.toStdString(), fileName.toStdString(),
                                        fileSize, segments, cookies.toStdString());
            });

    connect(m_pipeServer, &PipeServer::ytdlpRequested,
            this, &MainWindow::addYtdlpFromExternal);

    m_pipeServer->setDownloadListProvider([this]() -> QByteArray {
        auto downloads = m_manager->allDownloads();
        QJsonArray arr;
        for (auto& d : downloads) {
            QJsonObject obj;
            obj["id"]            = d.id;
            obj["url"]           = QString::fromStdString(d.url);
            obj["fileName"]      = QString::fromStdString(d.fileName);
            obj["state"]         = QString::fromUtf8(toString(d.state));
            obj["totalSize"]     = static_cast<qint64>(d.totalSize);
            qint64 dl = d.downloadedBytes;
            if (!d.segments.empty()) {
                dl = 0;
                for (auto& s : d.segments) dl += s.downloadedBytes;
            }
            obj["downloadedBytes"] = dl;
            arr.append(obj);
        }
        QJsonObject root;
        root["downloads"] = arr;
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    });

    if (!m_pipeServer->start()) {
        m_statusLabel->setText("Warning: named pipe server failed to start");
    } else {
        m_statusLabel->setText("Ready — extension connected via Native Messaging");
    }
}

// ---------------------------------------------------------------------------
// Register NMH manifest in Windows registry (HKCU, no admin required)
// ---------------------------------------------------------------------------
void MainWindow::registerNativeMessagingHost() {
    // The extension ID must match the key in extension/manifest.json
    static const QString kHostName    = kNmhHostName;
    static const QString kExtId       = kExtensionId;

    QString exePath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString manifestPath = QDir::toNativeSeparators(dataDir + "/nmh-manifest.json");

    // Write the NMH manifest JSON
    QJsonObject manifest;
    manifest["name"]        = kHostName;
    manifest["description"] = "CheckDown Download Manager — Native Messaging Host";
    manifest["path"]        = exePath;
    manifest["type"]        = "stdio";
    manifest["allowed_origins"] = QJsonArray{
        QString("chrome-extension://%1/").arg(kExtId)
    };

    QFile f(manifestPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        f.close();
    } else {
        LOG_WARN("registerNativeMessagingHost: could not write manifest to {}",
                 manifestPath.toStdString());
        return;
    }

    // Register for Chrome
    {
        QSettings reg(
            QString("HKEY_CURRENT_USER\\Software\\Google\\Chrome\\NativeMessagingHosts\\%1")
                .arg(kHostName),
            QSettings::NativeFormat);
        reg.setValue(".", manifestPath);
    }
    // Register for Edge
    {
        QSettings reg(
            QString("HKEY_CURRENT_USER\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\%1")
                .arg(kHostName),
            QSettings::NativeFormat);
        reg.setValue(".", manifestPath);
    }

    LOG_INFO("NMH registered: host={} path={}",
             kHostName.toStdString(), manifestPath.toStdString());
}

// ---------------------------------------------------------------------------
// External download (from Chrome extension / server)
// ---------------------------------------------------------------------------
void MainWindow::addDownloadFromExternal(const std::string& url,
                                          const std::string& fileName,
                                          int64_t /*fileSize*/,
                                          int segments,
                                          const std::string& cookies) {
    auto savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    std::filesystem::path saveDir = savePath.toStdString();

    int id = m_manager->addDownload(url, saveDir, fileName, segments, cookies);
    (void)id;

    m_model->setDownloads(m_manager->allDownloads());

    // Show notification if window is hidden
    if (!isVisible()) {
        m_trayManager->showNotification("Download Added",
            QString::fromStdString("Started: " + (fileName.empty() ? url : fileName)));
    }

    // Bring window to front
    onShowWindow();
}

void MainWindow::addYtdlpFromExternal(const QString& url, bool isPlaylist,
                                       const QStringList& cookieLines) {
    auto savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    std::filesystem::path saveDir = (savePath + "/Videos").toStdString();
    std::filesystem::create_directories(saveDir);

    QString label = isPlaylist ? "yt-dlp playlist" : "yt-dlp video";
    int id = m_manager->addYtdlpDownload(
        url.toStdString(), saveDir, label.toStdString(), cookieLines, isPlaylist);
    (void)id;

    m_model->setDownloads(m_manager->allDownloads());

    if (!isVisible())
        m_trayManager->showNotification("yt-dlp Download Added",
            isPlaylist ? "Playlist queued" : "Video queued");

    onShowWindow();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void MainWindow::onAddDownload() {
    // Make sure window is visible if triggered from tray
    if (!isVisible()) {
        show();
        activateWindow();
    }

    AddDownloadDialog dlg(this);
    dlg.setDefaultSegments(m_defaultSegments);
    if (dlg.exec() != QDialog::Accepted) return;

    auto url       = dlg.url();
    auto savePath  = dlg.savePath();
    auto fileName  = dlg.fileName();
    int  segments  = dlg.segmentCount();

    int id = m_manager->addDownload(url, savePath, fileName, segments);
    (void)id;

    m_model->setDownloads(m_manager->allDownloads());
}

void MainWindow::onPauseResume() {
    int id = selectedDownloadId();
    if (id < 0) return;

    auto srcIdx = m_proxyModel->mapToSource(m_tableView->currentIndex());
    int row = srcIdx.row();
    auto state = m_model->stateAt(row);

    if (state == DownloadState::Downloading) {
        m_manager->pauseDownload(id);
    } else if (state == DownloadState::Paused ||
               state == DownloadState::Queued ||
               state == DownloadState::Failed) {
        m_manager->startDownload(id);
    }
}

void MainWindow::onCancel() {
    int id = selectedDownloadId();
    if (id < 0) return;

    m_manager->cancelDownload(id);
    m_model->setDownloads(m_manager->allDownloads());
}

void MainWindow::onRemove() {
    int id = selectedDownloadId();
    if (id < 0) return;

    auto srcIdx = m_proxyModel->mapToSource(m_tableView->currentIndex());
    int row = srcIdx.row();
    auto state = m_model->stateAt(row);

    // Only confirm when removing an active download (would cancel it)
    bool isActive = (state == DownloadState::Downloading ||
                     state == DownloadState::Queued      ||
                     state == DownloadState::Paused);
    if (isActive) {
        auto ans = QMessageBox::question(this, "Remove Download",
            "This download is still active. Cancel and remove it?");
        if (ans != QMessageBox::Yes) return;
    }

    m_manager->removeDownload(id);
    m_model->setDownloads(m_manager->allDownloads());
}

void MainWindow::onClearFinished() {
    m_manager->clearFinished();
    m_model->setDownloads(m_manager->allDownloads());
}

void MainWindow::onSelectionChanged() {
    updateButtonStates();
}

void MainWindow::onDownloadProgress(checkdown::TaskProgress progress) {
    m_model->updateDownload(progress);
    updateButtonStates();
    updateStatusBar();
    updateWindowTitle();

    if (progress.state == DownloadState::Completed) {
        // Refresh model so filename/savePath are current
        m_model->setDownloads(m_manager->allDownloads());
        if (!isVisible()) {
            QString name = progress.fileName.empty()
                ? "A download" : QString::fromStdString(progress.fileName);
            m_trayManager->showNotification("Download Complete",
                name + " finished successfully.");
        }
    } else if (progress.state == DownloadState::Failed) {
        m_statusLabel->setText(QString("Failed: %1")
            .arg(QString::fromStdString(progress.errorMessage)));
    }
}

void MainWindow::onShowWindow() {
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::onSettings() {
    // Make sure window is visible if triggered from tray
    if (!isVisible()) {
        show();
        activateWindow();
    }

    SettingsDialog dlg(this);
    dlg.setMinimizeToTray(m_minimizeToTray);
    dlg.setStartWithWindows(m_settings.value("startWithWindows", false).toBool());
    dlg.setMaxConcurrent(m_manager->maxConcurrent());
    dlg.setDefaultSegments(m_defaultSegments);

    if (dlg.exec() != QDialog::Accepted) return;

    m_minimizeToTray  = dlg.minimizeToTray();
    m_defaultSegments = dlg.defaultSegments();
    bool startWithWin = dlg.startWithWindows();
    int  maxConc      = dlg.maxConcurrent();

    m_manager->setMaxConcurrent(maxConc);

    m_settings.setValue("minimizeToTray",  m_minimizeToTray);
    m_settings.setValue("startWithWindows", startWithWin);
    m_settings.setValue("maxConcurrent",   maxConc);
    m_settings.setValue("defaultSegments", m_defaultSegments);

    // Update Windows startup registry
    QSettings regSettings(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);

    if (startWithWin) {
        QString appPath = QApplication::applicationFilePath();
        appPath.replace('/', '\\');
        regSettings.setValue("CheckDown", QString("\"%1\" --minimized").arg(appPath));
    } else {
        regSettings.remove("CheckDown");
    }
}

void MainWindow::onExitApp() {
    m_reallyQuit = true;

    // Pause all active downloads and save state
    auto downloads = m_manager->allDownloads();
    for (auto& d : downloads) {
        if (d.state == DownloadState::Downloading) {
            if (d.isYtdlp)
                m_manager->cancelDownload(d.id);
            else
                m_manager->pauseDownload(d.id);
        }
    }
    m_manager->saveState();
    saveSettings();

    QApplication::quit();
}

void MainWindow::onTableDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    auto srcIdx = m_proxyModel->mapToSource(index);
    int row = srcIdx.row();
    if (m_model->stateAt(row) != DownloadState::Completed) return;

    QString path = m_model->savePathAt(row);
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (fi.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MainWindow::onTableContextMenu(const QPoint& pos) {
    QModelIndex idx = m_tableView->indexAt(pos);
    if (!idx.isValid()) return;

    auto srcIdx = m_proxyModel->mapToSource(idx);
    int row      = srcIdx.row();
    int id       = m_model->downloadIdAt(row);
    auto state   = m_model->stateAt(row);
    bool isYtdlp = m_model->isYtdlpAt(row);
    QString savePath = m_model->savePathAt(row);

    QMenu menu(this);

    // Pause/Resume — yt-dlp tasks don't support pause
    if (!isYtdlp) {
        if (state == DownloadState::Downloading) {
            menu.addAction("Pause", [this, id] {
                m_manager->pauseDownload(id);
            });
        } else if (state == DownloadState::Paused ||
                   state == DownloadState::Queued  ||
                   state == DownloadState::Failed) {
            menu.addAction("Resume / Start", [this, id] {
                m_manager->startDownload(id);
            });
        }
    }

    if (state == DownloadState::Downloading ||
        state == DownloadState::Paused      ||
        state == DownloadState::Queued) {
        menu.addAction("Cancel", [this, id] {
            m_manager->cancelDownload(id);
            m_model->setDownloads(m_manager->allDownloads());
        });
    }

    menu.addSeparator();

    menu.addAction("Remove", [this, id] {
        m_manager->removeDownload(id);
        m_model->setDownloads(m_manager->allDownloads());
    });

    if (state == DownloadState::Completed && !savePath.isEmpty()) {
        menu.addSeparator();

        if (isYtdlp) {
            // savePath is the downloads folder for yt-dlp tasks
            menu.addAction("Open Downloads Folder", [savePath] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
            });
        } else {
            menu.addAction("Open File", [savePath] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
            });
            menu.addAction("Show in Folder", [savePath] {
                QFileInfo fi(savePath);
                QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
            });
        }
    }

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void MainWindow::updateButtonStates() {
    // Map proxy row → source row
    auto proxyIdx = m_tableView->currentIndex();
    auto srcIdx = m_proxyModel->mapToSource(proxyIdx);
    int row = srcIdx.row();
    bool hasSelection = srcIdx.isValid() && row >= 0 && row < m_model->rowCount();

    // Show/hide empty state
    m_emptyLabel->setVisible(m_model->rowCount() == 0);
    if (m_model->rowCount() == 0) {
        m_emptyLabel->setGeometry(m_tableView->viewport()->rect());
    }

    if (!hasSelection) {
        m_pauseAction->setEnabled(false);
        m_pauseAction->setText("Pause / Resume");
        m_cancelAction->setEnabled(false);
        m_removeAction->setEnabled(false);
        return;
    }

    auto state   = m_model->stateAt(row);
    bool isYtdlp = m_model->isYtdlpAt(row);

    // yt-dlp tasks don't support pause/resume
    bool canPauseResume = !isYtdlp &&
                          (state == DownloadState::Downloading ||
                           state == DownloadState::Paused ||
                           state == DownloadState::Queued ||
                           state == DownloadState::Failed);
    bool canCancel      = (state == DownloadState::Downloading ||
                           state == DownloadState::Paused ||
                           state == DownloadState::Queued);

    m_pauseAction->setEnabled(canPauseResume);
    m_cancelAction->setEnabled(canCancel);
    m_removeAction->setEnabled(true);

    bool isPausing = (state == DownloadState::Downloading);
    m_pauseAction->setText(isPausing ? "Pause" : "Resume");
    m_pauseAction->setIcon(QApplication::style()->standardIcon(
        isPausing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void MainWindow::updateStatusBar() {
    int active = m_model->activeCount();
    int total  = m_model->rowCount();
    if (active > 0) {
        double speed = m_model->totalActiveSpeed();
        QString speedStr = DownloadTableModel::formatBytes(static_cast<int64_t>(speed));
        m_statusLabel->setText(
            QString("%1 downloading  |  %2 total  |  %3/s")
                .arg(active)
                .arg(total)
                .arg(speedStr)
        );
    } else if (total > 0) {
        m_statusLabel->setText(QString("%1 download%2").arg(total).arg(total == 1 ? "" : "s"));
    } else {
        m_statusLabel->setText("Ready — drag a URL here or press Ctrl+N");
    }
}

int MainWindow::selectedDownloadId() const {
    auto proxyIdx = m_tableView->currentIndex();
    auto srcIdx = m_proxyModel->mapToSource(proxyIdx);
    return m_model->downloadIdAt(srcIdx.row());
}

// ---------------------------------------------------------------------------
// Drag & Drop — accept URLs dragged onto the window
// ---------------------------------------------------------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    // Try URLs first
    for (const auto& url : event->mimeData()->urls()) {
        auto str = url.toString();
        if (str.startsWith("http://") || str.startsWith("https://")) {
            addDownloadFromExternal(str.toStdString());
            return;
        }
    }
    // Fall back to plain text
    auto text = event->mimeData()->text().trimmed();
    if (text.startsWith("http://") || text.startsWith("https://"))
        addDownloadFromExternal(text.toStdString());
}

// ---------------------------------------------------------------------------
// Window title — show active count
// ---------------------------------------------------------------------------
void MainWindow::updateWindowTitle() {
    int active = m_model->activeCount();
    if (active > 0)
        setWindowTitle(QString("CheckDown  (%1 active)").arg(active));
    else
        setWindowTitle("CheckDown");
}

// ---------------------------------------------------------------------------
// Modern stylesheet
// ---------------------------------------------------------------------------
void MainWindow::applyStylesheet() {
    qApp->setStyleSheet(R"(
        /* ─── Global ─── */
        QMainWindow, QDialog, QMessageBox {
            background: #1e1e2e;
            color: #cdd6f4;
        }

        QLabel {
            color: #cdd6f4;
        }

        /* ─── Toolbar ─── */
        QToolBar {
            background: #181825;
            border: none;
            border-bottom: 1px solid #313244;
            padding: 4px 6px;
            spacing: 3px;
        }
        QToolBar::separator {
            width: 1px;
            background: #313244;
            margin: 4px 6px;
        }
        QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 5px 10px;
            font-size: 12px;
            font-weight: 500;
            color: #bac2de;
        }
        QToolButton:hover {
            background: rgba(137, 180, 250, 0.10);
            border-color: rgba(137, 180, 250, 0.20);
            color: #cdd6f4;
        }
        QToolButton:pressed {
            background: rgba(137, 180, 250, 0.18);
        }
        QToolButton:disabled {
            color: #585b70;
        }

        /* ─── Table ─── */
        QTableView {
            background: #1e1e2e;
            alternate-background-color: #1a1a28;
            border: none;
            selection-background-color: rgba(137, 180, 250, 0.12);
            selection-color: #cdd6f4;
            gridline-color: transparent;
            font-size: 12px;
            color: #cdd6f4;
        }
        QTableView::item {
            padding: 2px 6px;
            border-bottom: 1px solid #252536;
        }
        QTableView::item:selected {
            background: rgba(137, 180, 250, 0.12);
        }
        QTableView::item:hover {
            background: rgba(137, 180, 250, 0.06);
        }

        QHeaderView::section {
            background: #181825;
            border: none;
            border-bottom: 2px solid #313244;
            border-right: 1px solid #252536;
            padding: 6px 8px;
            font-size: 11px;
            font-weight: 600;
            color: #7f849c;
            text-transform: uppercase;
        }
        QHeaderView::section:last {
            border-right: none;
        }
        QHeaderView::down-arrow {
            image: none;
            width: 0;
        }
        QHeaderView::up-arrow {
            image: none;
            width: 0;
        }

        /* ─── Status Bar ─── */
        QStatusBar {
            background: #181825;
            border-top: 1px solid #313244;
            font-size: 11px;
            color: #7f849c;
            padding: 2px 8px;
        }

        /* ─── Dialogs ─── */
        QGroupBox {
            font-weight: 600;
            font-size: 12px;
            color: #cdd6f4;
            border: 1px solid #313244;
            border-radius: 8px;
            margin-top: 14px;
            padding: 16px 12px 10px 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 6px;
            background: #1e1e2e;
        }

        QLineEdit {
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 6px 8px;
            font-size: 12px;
            background: #11111b;
            color: #cdd6f4;
            selection-background-color: rgba(137, 180, 250, 0.3);
            selection-color: #cdd6f4;
        }
        QLineEdit:focus {
            border-color: #89b4fa;
        }
        QLineEdit:disabled {
            color: #585b70;
            background: #181825;
        }

        QSpinBox {
            border: 1px solid #45475a;
            border-radius: 4px;
            padding: 4px 6px;
            font-size: 12px;
            background: #11111b;
            color: #cdd6f4;
        }
        QSpinBox:focus {
            border-color: #89b4fa;
        }
        QSpinBox::up-button {
            subcontrol-origin: border;
            subcontrol-position: top right;
            width: 20px;
            border-left: 1px solid #45475a;
            border-top-right-radius: 4px;
            background: #313244;
        }
        QSpinBox::up-button:hover {
            background: #45475a;
        }
        QSpinBox::up-arrow {
            image: url(:/arrow-up.png);
            width: 10px;
            height: 8px;
        }
        QSpinBox::down-button {
            subcontrol-origin: border;
            subcontrol-position: bottom right;
            width: 20px;
            border-left: 1px solid #45475a;
            border-bottom-right-radius: 4px;
            background: #313244;
        }
        QSpinBox::down-button:hover {
            background: #45475a;
        }
        QSpinBox::down-arrow {
            image: url(:/arrow-down.png);
            width: 10px;
            height: 8px;
        }

        QPushButton {
            background: #313244;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 6px 16px;
            font-size: 12px;
            font-weight: 500;
            color: #cdd6f4;
        }
        QPushButton:hover {
            background: #3b3d52;
            border-color: #585b70;
        }
        QPushButton:pressed {
            background: #45475a;
        }
        QPushButton:default {
            background: #89b4fa;
            color: #1e1e2e;
            border-color: #74a8f7;
            font-weight: 600;
        }
        QPushButton:default:hover {
            background: #74a8f7;
        }
        QPushButton:default:pressed {
            background: #5e9af5;
        }

        QCheckBox {
            font-size: 12px;
            color: #cdd6f4;
            spacing: 6px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1px solid #45475a;
            border-radius: 4px;
            background: #11111b;
        }
        QCheckBox::indicator:checked {
            background: #89b4fa;
            border-color: #74a8f7;
        }
        QCheckBox::indicator:hover {
            border-color: #585b70;
        }

        /* ─── Scrollbar ─── */
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: rgba(205, 214, 244, 0.12);
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(205, 214, 244, 0.22);
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }

        /* ─── Context Menu ─── */
        QMenu {
            background: #1e1e2e;
            border: 1px solid #313244;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 24px 6px 12px;
            border-radius: 4px;
            font-size: 12px;
            color: #cdd6f4;
        }
        QMenu::item:selected {
            background: rgba(137, 180, 250, 0.12);
            color: #cdd6f4;
        }
        QMenu::separator {
            height: 1px;
            background: #313244;
            margin: 4px 8px;
        }

        /* ─── Tooltip ─── */
        QToolTip {
            background: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 11px;
        }
    )");
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_emptyLabel && m_tableView)
        m_emptyLabel->setGeometry(m_tableView->viewport()->rect());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_minimizeToTray && !m_reallyQuit) {
        hide();
        event->ignore();
        return;
    }

    auto downloads = m_manager->allDownloads();
    for (auto& d : downloads) {
        if (d.state == DownloadState::Downloading) {
            if (d.isYtdlp)
                m_manager->cancelDownload(d.id);
            else
                m_manager->pauseDownload(d.id);
        }
    }
    m_manager->saveState();
    saveSettings();
    event->accept();
}

void MainWindow::loadSettings() {
    m_minimizeToTray  = m_settings.value("minimizeToTray",  true).toBool();
    m_defaultSegments = m_settings.value("defaultSegments", kDefaultSegmentCount).toInt();
}

void MainWindow::saveSettings() {
    m_settings.setValue("minimizeToTray",  m_minimizeToTray);
    m_settings.setValue("defaultSegments", m_defaultSegments);
}

} // namespace checkdown
