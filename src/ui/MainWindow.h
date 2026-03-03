#pragma once

#include "../core/Types.h"
#include "../core/DownloadManager.h"
#include "DownloadTableModel.h"
#include "ProgressDelegate.h"

#include <QMainWindow>
#include <QTableView>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QSettings>
#include <memory>

namespace checkdown {

class TrayManager;
class LocalServer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Add a download from an external source (e.g., local server / Chrome extension).
    void addDownloadFromExternal(const std::string& url,
                                 const std::string& fileName = {},
                                 int64_t fileSize = -1,
                                 int segments = kDefaultSegmentCount);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAddDownload();
    void onPauseResume();
    void onCancel();
    void onRemove();
    void onClearFinished();
    void onSelectionChanged();
    void onDownloadProgress(checkdown::TaskProgress progress);
    void onShowWindow();
    void onSettings();
    void onExitApp();

signals:
    void downloadProgressReceived(checkdown::TaskProgress progress);

private:
    void createActions();
    void createToolBar();
    void createStatusBar();
    void setupTray();
    void setupLocalServer();
    void updateButtonStates();
    void loadSettings();
    void saveSettings();

    int selectedDownloadId() const;

    std::unique_ptr<DownloadManager> m_manager;
    DownloadTableModel*              m_model     = nullptr;
    ProgressDelegate*                m_delegate  = nullptr;
    QTableView*                      m_tableView = nullptr;

    QAction* m_addAction          = nullptr;
    QAction* m_pauseAction        = nullptr;
    QAction* m_cancelAction       = nullptr;
    QAction* m_removeAction       = nullptr;
    QAction* m_clearFinishedAction = nullptr;

    QLabel*  m_statusLabel  = nullptr;

    // System tray
    TrayManager* m_trayManager    = nullptr;
    bool         m_minimizeToTray = true;
    bool         m_reallyQuit     = false;

    // Local server (for Chrome extension)
    LocalServer* m_localServer = nullptr;

    // Settings
    QSettings m_settings;
};

} // namespace checkdown
