#include "SettingsDialog.h"
#include "../core/Logger.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QLabel>

namespace checkdown {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);

    // --- General group ---
    auto* generalGroup  = new QGroupBox("General");
    auto* generalLayout = new QVBoxLayout(generalGroup);

    m_minimizeToTray = new QCheckBox("Minimize to system tray on close");
    m_minimizeToTray->setToolTip("When closing the window, minimize to the system tray instead of quitting.");
    generalLayout->addWidget(m_minimizeToTray);

    m_startWithWindows = new QCheckBox("Start with Windows");
    m_startWithWindows->setToolTip("Automatically start CheckDown when Windows starts.");
    generalLayout->addWidget(m_startWithWindows);

    layout->addWidget(generalGroup);

    // --- Downloads group ---
    auto* dlGroup  = new QGroupBox("Downloads");
    auto* dlForm   = new QFormLayout(dlGroup);

    m_maxConcurrent = new QSpinBox;
    m_maxConcurrent->setRange(1, 10);
    m_maxConcurrent->setValue(3);
    m_maxConcurrent->setToolTip("Maximum number of downloads running simultaneously.");
    dlForm->addRow("Max concurrent:", m_maxConcurrent);

    m_defaultSegments = new QSpinBox;
    m_defaultSegments->setRange(1, 32);
    m_defaultSegments->setValue(8);
    m_defaultSegments->setToolTip("Default number of parallel segments per download.");
    dlForm->addRow("Default segments:", m_defaultSegments);

    layout->addWidget(dlGroup);

    // --- Button row with View Log + OK/Cancel ---
    auto* btnRow  = new QHBoxLayout;
    auto* logBtn  = new QPushButton("View Log File");
    logBtn->setToolTip("Open the application log in your default text editor.");
    connect(logBtn, &QPushButton::clicked, this, [] {
        auto path = Logger::instance().logPath();
        if (!path.empty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QString::fromStdString(path.string())));
    });
    btnRow->addWidget(logBtn);
    btnRow->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnRow->addWidget(buttons);

    layout->addLayout(btnRow);

    layout->addStretch();
}

bool SettingsDialog::minimizeToTray()  const { return m_minimizeToTray->isChecked(); }
bool SettingsDialog::startWithWindows() const { return m_startWithWindows->isChecked(); }
int  SettingsDialog::maxConcurrent()   const { return m_maxConcurrent->value(); }
int  SettingsDialog::defaultSegments() const { return m_defaultSegments->value(); }

void SettingsDialog::setMinimizeToTray(bool v)  { m_minimizeToTray->setChecked(v); }
void SettingsDialog::setStartWithWindows(bool v) { m_startWithWindows->setChecked(v); }
void SettingsDialog::setMaxConcurrent(int v)     { m_maxConcurrent->setValue(v); }
void SettingsDialog::setDefaultSegments(int v)   { m_defaultSegments->setValue(v); }

} // namespace checkdown
