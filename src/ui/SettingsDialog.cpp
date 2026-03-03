#include "SettingsDialog.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>

namespace checkdown {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumWidth(360);

    auto* layout = new QVBoxLayout(this);

    // General group
    auto* generalGroup = new QGroupBox("General");
    auto* generalLayout = new QVBoxLayout(generalGroup);

    m_minimizeToTray = new QCheckBox("Minimize to system tray on close");
    m_minimizeToTray->setToolTip("When closing the window, minimize to the system tray instead of quitting.");
    generalLayout->addWidget(m_minimizeToTray);

    m_startWithWindows = new QCheckBox("Start with Windows");
    m_startWithWindows->setToolTip("Automatically start CheckDown when Windows starts.");
    generalLayout->addWidget(m_startWithWindows);

    layout->addWidget(generalGroup);

    // Buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    layout->addStretch();
}

bool SettingsDialog::minimizeToTray() const {
    return m_minimizeToTray->isChecked();
}

bool SettingsDialog::startWithWindows() const {
    return m_startWithWindows->isChecked();
}

void SettingsDialog::setMinimizeToTray(bool enabled) {
    m_minimizeToTray->setChecked(enabled);
}

void SettingsDialog::setStartWithWindows(bool enabled) {
    m_startWithWindows->setChecked(enabled);
}

} // namespace checkdown
