#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>

#include <string>
#include <filesystem>

namespace checkdown {

class AddDownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddDownloadDialog(QWidget* parent = nullptr);

    [[nodiscard]] std::string           url() const;
    [[nodiscard]] std::filesystem::path savePath() const;
    [[nodiscard]] std::string           fileName() const;
    [[nodiscard]] int                   segmentCount() const;

    void setDefaultSegments(int count);

private slots:
    void onBrowse();
    void onUrlChanged();
    void onAccept();

private:
    QLineEdit*   m_urlEdit           = nullptr;
    QLineEdit*   m_fileNameEdit      = nullptr;
    QLineEdit*   m_savePathEdit      = nullptr;
    QSpinBox*    m_segmentSpin       = nullptr;
    QPushButton* m_browseBtn         = nullptr;
    bool         m_fileNameEdited    = false;  // true once user manually types a filename
};

} // namespace checkdown
