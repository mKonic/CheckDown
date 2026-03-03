#include "MainWindow.h"
#include "AddDownloadDialog.h"
#include "TrayManager.h"
#include "SettingsDialog.h"
#include "../server/LocalServer.h"

#include <QCloseEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFileDialog>
#include <QApplication>
#include <filesystem>

namespace checkdown {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, "CheckDown", "CheckDown")
{
    setWindowTitle("CheckDown - Download Manager");
    resize(960, 520);

    // State directory
    auto configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    std::filesystem::path stateDir = configDir.toStdString();

    // Download manager with progress bridge
    m_manager = std::make_unique<DownloadManager>(
        stateDir,
        [this](const TaskProgress& p) {
            // This fires on worker threads — emit signal for queued delivery
            emit downloadProgressReceived(p);
        }
    );

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
    m_tableView->verticalHeader()->hide();
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->setColumnWidth(DownloadTableModel::ColId, 40);
    m_tableView->setColumnWidth(DownloadTableModel::ColFileName, 200);
    m_tableView->setColumnWidth(DownloadTableModel::ColSize, 90);
    m_tableView->setColumnWidth(DownloadTableModel::ColProgress, 120);
    m_tableView->setColumnWidth(DownloadTableModel::ColSpeed, 100);
    m_tableView->setColumnWidth(DownloadTableModel::ColStatus, 90);

    setCentralWidget(m_tableView);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    createActions();
    createToolBar();
    createStatusBar();

    // Load settings before setting up tray (minimizeToTray affects behavior)
    loadSettings();

    // System tray
    setupTray();

    // Local HTTP server for Chrome extension
    setupLocalServer();

    // Load saved download state
    m_manager->loadState();
    m_model->setDownloads(m_manager->allDownloads());
    updateButtonStates();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Actions & Toolbar
// ---------------------------------------------------------------------------
void MainWindow::createActions() {
    m_addAction = new QAction("Add URL", this);
    m_addAction->setShortcut(QKeySequence("Ctrl+N"));
    connect(m_addAction, &QAction::triggered, this, &MainWindow::onAddDownload);

    m_pauseAction = new QAction("Pause / Resume", this);
    m_pauseAction->setEnabled(false);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPauseResume);

    m_cancelAction = new QAction("Cancel", this);
    m_cancelAction->setEnabled(false);
    connect(m_cancelAction, &QAction::triggered, this, &MainWindow::onCancel);

    m_removeAction = new QAction("Remove", this);
    m_removeAction->setEnabled(false);
    m_removeAction->setShortcut(QKeySequence::Delete);
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::onRemove);

    m_clearFinishedAction = new QAction("Clear Finished", this);
    connect(m_clearFinishedAction, &QAction::triggered, this, &MainWindow::onClearFinished);
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
// Local HTTP Server
// ---------------------------------------------------------------------------
void MainWindow::setupLocalServer() {
    m_localServer = new LocalServer(this);

    connect(m_localServer, &LocalServer::downloadRequested,
            this, [this](const QString& url, const QString& fileName,
                         int64_t fileSize, int segments) {
                addDownloadFromExternal(url.toStdString(), fileName.toStdString(),
                                         fileSize, segments);
            });

    if (!m_localServer->start()) {
        m_statusLabel->setText("Warning: Local server failed to start on port 18693");
    } else {
        m_statusLabel->setText("Ready — listening on localhost:18693");
    }
}

// ---------------------------------------------------------------------------
// External download (from Chrome extension / server)
// ---------------------------------------------------------------------------
void MainWindow::addDownloadFromExternal(const std::string& url,
                                          const std::string& fileName,
                                          int64_t /*fileSize*/,
                                          int segments) {
    auto savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    std::filesystem::path saveDir = savePath.toStdString();

    int id = m_manager->addDownload(url, saveDir, fileName, segments);
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
    if (dlg.exec() != QDialog::Accepted) return;

    auto url       = dlg.url();
    auto savePath  = dlg.savePath();
    auto fileName  = dlg.fileName();
    int  segments  = dlg.segmentCount();

    int id = m_manager->addDownload(url, savePath, fileName, segments);
    (void)id;

    // Refresh table
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

    auto ans = QMessageBox::question(this, "Remove Download",
        "Remove this download from the list?");
    if (ans != QMessageBox::Yes) return;

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

    if (progress.state == DownloadState::Completed) {
        m_statusLabel->setText("Download completed");
        // Tray notification if hidden
        if (!isVisible()) {
            m_trayManager->showNotification("Download Complete",
                "A download has finished successfully.");
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
    dlg.setStartWithWindows(
        m_settings.value("startWithWindows", false).toBool());

    if (dlg.exec() != QDialog::Accepted) return;

    m_minimizeToTray = dlg.minimizeToTray();
    bool startWithWin = dlg.startWithWindows();

    // Save settings
    m_settings.setValue("minimizeToTray", m_minimizeToTray);
    m_settings.setValue("startWithWindows", startWithWin);

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
        if (d.state == DownloadState::Downloading)
            m_manager->pauseDownload(d.id);
    }
    m_manager->saveState();
    saveSettings();

    QApplication::quit();
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

    auto state = m_model->stateAt(row);

    bool canPauseResume = (state == DownloadState::Downloading ||
                           state == DownloadState::Paused ||
                           state == DownloadState::Queued ||
                           state == DownloadState::Failed);
    bool canCancel      = (state == DownloadState::Downloading ||
                           state == DownloadState::Paused ||
                           state == DownloadState::Queued);
    bool canRemove      = true;

    m_pauseAction->setEnabled(canPauseResume);
    m_cancelAction->setEnabled(canCancel);
    m_removeAction->setEnabled(canRemove);

    if (state == DownloadState::Downloading)
        m_pauseAction->setText("Pause");
    else
        m_pauseAction->setText("Resume");
}

int MainWindow::selectedDownloadId() const {
    int row = m_tableView->currentIndex().row();
    return m_model->downloadIdAt(row);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_minimizeToTray && !m_reallyQuit) {
        // Minimize to system tray instead of quitting
        hide();
        event->ignore();

        m_trayManager->showNotification("CheckDown",
            "CheckDown is still running in the system tray.\n"
            "Double-click the tray icon to restore.");
        return;
    }

    // Really quitting — pause all active downloads and save state
    auto downloads = m_manager->allDownloads();
    for (auto& d : downloads) {
        if (d.state == DownloadState::Downloading)
            m_manager->pauseDownload(d.id);
    }
    m_manager->saveState();
    saveSettings();
    event->accept();
}

void MainWindow::loadSettings() {
    m_minimizeToTray = m_settings.value("minimizeToTray", true).toBool();
}

void MainWindow::saveSettings() {
    m_settings.setValue("minimizeToTray", m_minimizeToTray);
}

} // namespace checkdown
