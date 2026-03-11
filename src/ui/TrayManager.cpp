#include "TrayManager.h"

#include <QApplication>
#include <QWidget>

namespace checkdown {

TrayManager::TrayManager(QWidget* parentWindow, QObject* parent)
    : QObject(parent)
{
    createTrayMenu();

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(QApplication::windowIcon());
    m_trayIcon->setToolTip("CheckDown - Download Manager");
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &TrayManager::onTrayActivated);

    m_trayIcon->show();
}

TrayManager::~TrayManager() = default;

void TrayManager::createTrayMenu() {
    m_trayMenu = new QMenu;

    m_showAction = m_trayMenu->addAction("Show CheckDown");
    QFont boldFont = m_showAction->font();
    boldFont.setBold(true);
    m_showAction->setFont(boldFont);
    connect(m_showAction, &QAction::triggered, this, &TrayManager::showWindowRequested);

    m_trayMenu->addSeparator();

    m_addUrlAction = m_trayMenu->addAction("Add URL...");
    connect(m_addUrlAction, &QAction::triggered, this, &TrayManager::addUrlRequested);

    m_settingsAction = m_trayMenu->addAction("Settings...");
    connect(m_settingsAction, &QAction::triggered, this, &TrayManager::settingsRequested);

    m_trayMenu->addSeparator();

    m_exitAction = m_trayMenu->addAction("Exit");
    connect(m_exitAction, &QAction::triggered, this, &TrayManager::exitRequested);
}

void TrayManager::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick ||
        reason == QSystemTrayIcon::Trigger) {
        emit showWindowRequested();
    }
}

void TrayManager::showNotification(const QString& title, const QString& message,
                                    QSystemTrayIcon::MessageIcon icon, int msecs) {
    if (m_trayIcon->supportsMessages()) {
        m_trayIcon->showMessage(title, message, icon, msecs);
    }
}

} // namespace checkdown
