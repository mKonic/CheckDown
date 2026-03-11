#include "MainWindow.h"
#include "AddDownloadDialog.h"
#include "TrayManager.h"
#include "SettingsDialog.h"
#include "../server/PipeServer.h"
#include "../core/Logger.h"
#include "../core/Version.h"

#include <QCloseEvent>
#include <QStyle>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileDialog>
#include <QApplication>
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
#include <filesystem>

namespace checkdown {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, "CheckDown", "CheckDown")
{
    setWindowTitle("CheckDown - Download Manager");
    resize(1020, 520);

    // State / log directory
    auto configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    std::filesystem::path stateDir = configDir.toStdString();

    // Init logger before anything else so all operations are captured
    Logger::instance().init(stateDir / "checkdown.log");
    LOG_INFO("=== MainWindow initialising ===");

    // Load settings early so m_defaultSegments and maxConcurrent are ready
    loadSettings();

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

    // Table model + view
    m_model    = new DownloadTableModel(this);
    m_delegate = new ProgressDelegate(this);

    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setItemDelegateForColumn(DownloadTableModel::ColProgress, m_delegate);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setShowGrid(false);
    m_tableView->verticalHeader()->hide();
    m_tableView->verticalHeader()->setDefaultSectionSize(26);
    m_tableView->horizontalHeader()->setStretchLastSection(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(
        DownloadTableModel::ColFileName, QHeaderView::Stretch);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableView->setColumnWidth(DownloadTableModel::ColId,        36);
    m_tableView->setColumnWidth(DownloadTableModel::ColSize,      90);
    m_tableView->setColumnWidth(DownloadTableModel::ColProgress, 140);
    m_tableView->setColumnWidth(DownloadTableModel::ColSpeed,    100);
    m_tableView->setColumnWidth(DownloadTableModel::ColEta,       72);
    m_tableView->setColumnWidth(DownloadTableModel::ColStatus,    90);
    m_tableView->setColumnHidden(DownloadTableModel::ColUrl, true);

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
    // Note: setupLocalServer() already set the status label appropriately
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
}

void MainWindow::createToolBar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(m_addAction);
    tb->addSeparator();
    tb->addAction(m_pauseAction);
    tb->addAction(m_cancelAction);
    tb->addAction(m_removeAction);
    tb->addSeparator();
    tb->addAction(m_clearFinishedAction);
    tb->addSeparator();
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
            qint64 dl = 0;
            for (auto& s : d.segments) dl += s.downloadedBytes;
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

    int row = m_tableView->currentIndex().row();
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

    int row = m_tableView->currentIndex().row();
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
    int row = index.row();
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

    int row      = idx.row();
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
    int row = m_tableView->currentIndex().row();
    bool hasSelection = row >= 0 && row < m_model->rowCount();

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
        m_statusLabel->setText(
            QString("%1 active, %2 total  |  %3/s")
                .arg(active)
                .arg(total)
                .arg(DownloadTableModel::formatBytes(static_cast<int64_t>(speed)))
        );
    } else if (total > 0) {
        m_statusLabel->setText(QString("%1 download%2").arg(total).arg(total == 1 ? "" : "s"));
    } else {
        m_statusLabel->setText("Ready");
    }
}

int MainWindow::selectedDownloadId() const {
    int row = m_tableView->currentIndex().row();
    return m_model->downloadIdAt(row);
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
