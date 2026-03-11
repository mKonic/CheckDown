#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QDialogButtonBox>

namespace checkdown {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    bool minimizeToTray()  const;
    bool startWithWindows() const;
    int  maxConcurrent()   const;
    int  defaultSegments() const;

    void setMinimizeToTray(bool enabled);
    void setStartWithWindows(bool enabled);
    void setMaxConcurrent(int value);
    void setDefaultSegments(int value);

private:
    QCheckBox* m_minimizeToTray   = nullptr;
    QCheckBox* m_startWithWindows = nullptr;
    QSpinBox*  m_maxConcurrent    = nullptr;
    QSpinBox*  m_defaultSegments  = nullptr;
};

} // namespace checkdown
