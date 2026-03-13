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
#include <QSortFilterProxyModel>
#include <memory>

namespace checkdown {

class TrayManager;
class PipeServer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Add a regular HTTP download from an external source.
    void addDownloadFromExternal(const std::string& url,
                                 const std::string& fileName = {},
                                 int64_t fileSize = -1,
                                 int segments = kDefaultSegmentCount,
                                 const std::string& cookies = {});

    /// Add a yt-dlp download from the Chrome extension.
    void addYtdlpFromExternal(const QString& url, bool isPlaylist,
                              const QStringList& cookieLines);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

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
    void onTableDoubleClicked(const QModelIndex& index);
    void onTableContextMenu(const QPoint& pos);

signals:
    void downloadProgressReceived(checkdown::TaskProgress progress);

private:
    void createActions();
    void createToolBar();
    void createStatusBar();
    void setupTray();
    void setupPipeServer();
    void registerNativeMessagingHost();
    void updateButtonStates();
    void updateStatusBar();
    void updateWindowTitle();
    void applyStylesheet();
    void loadSettings();
    void saveSettings();

    int selectedDownloadId() const;

    std::unique_ptr<DownloadManager> m_manager;
    DownloadTableModel*              m_model      = nullptr;
    QSortFilterProxyModel*           m_proxyModel = nullptr;
    ProgressDelegate*                m_delegate   = nullptr;
    QTableView*                      m_tableView  = nullptr;
    QLabel*                          m_emptyLabel = nullptr;

    QAction* m_addAction           = nullptr;
    QAction* m_pauseAction         = nullptr;
    QAction* m_cancelAction        = nullptr;
    QAction* m_removeAction        = nullptr;
    QAction* m_clearFinishedAction = nullptr;
    QAction* m_settingsAction      = nullptr;

    QLabel*  m_statusLabel  = nullptr;

    // System tray
    TrayManager* m_trayManager    = nullptr;
    bool         m_minimizeToTray = true;
    bool         m_reallyQuit     = false;

    // Named-pipe server (for Chrome extension via NMH bridge)
    PipeServer* m_pipeServer = nullptr;

    // Settings
    QSettings m_settings;
    int       m_defaultSegments = kDefaultSegmentCount;
};

} // namespace checkdown
