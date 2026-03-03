#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QDialogButtonBox>

namespace checkdown {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    bool minimizeToTray() const;
    bool startWithWindows() const;

    void setMinimizeToTray(bool enabled);
    void setStartWithWindows(bool enabled);

private:
    QCheckBox* m_minimizeToTray   = nullptr;
    QCheckBox* m_startWithWindows = nullptr;
};

} // namespace checkdown
