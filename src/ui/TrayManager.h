#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>

namespace checkdown {

class TrayManager : public QObject {
    Q_OBJECT
public:
    explicit TrayManager(QWidget* parentWindow, QObject* parent = nullptr);
    ~TrayManager() override;

    void showNotification(const QString& title, const QString& message,
                          QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                          int msecs = 3000);

signals:
    void showWindowRequested();
    void addUrlRequested();
    void settingsRequested();
    void exitRequested();

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void createTrayMenu();

    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu*           m_trayMenu = nullptr;

    QAction* m_showAction     = nullptr;
    QAction* m_addUrlAction   = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_exitAction     = nullptr;
};

} // namespace checkdown
