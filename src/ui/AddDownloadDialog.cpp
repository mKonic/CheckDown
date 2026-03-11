#include "AddDownloadDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QApplication>
#include <QClipboard>
#include <QUrl>
#include <filesystem>

namespace checkdown {

AddDownloadDialog::AddDownloadDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Add Download");
    setMinimumWidth(520);

    auto* form = new QFormLayout;

    // URL
    m_urlEdit = new QLineEdit;
    m_urlEdit->setPlaceholderText("https://example.com/file.zip");
    form->addRow("URL:", m_urlEdit);

    // File name
    m_fileNameEdit = new QLineEdit;
    m_fileNameEdit->setPlaceholderText("(auto-detected from URL)");
    form->addRow("File name:", m_fileNameEdit);

    // Save path
    auto* pathLayout = new QHBoxLayout;
    m_savePathEdit = new QLineEdit;
    auto defaultDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_savePathEdit->setText(defaultDir);
    m_browseBtn = new QPushButton("Browse...");
    pathLayout->addWidget(m_savePathEdit, 1);
    pathLayout->addWidget(m_browseBtn);
    form->addRow("Save to:", pathLayout);

    // Segments
    m_segmentSpin = new QSpinBox;
    m_segmentSpin->setRange(1, 32);
    m_segmentSpin->setValue(8);
    form->addRow("Segments:", m_segmentSpin);

    // Buttons
    auto* btnLayout = new QHBoxLayout;
    auto* okBtn     = new QPushButton("Download");
    auto* cancelBtn = new QPushButton("Cancel");
    okBtn->setDefault(true);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_browseBtn, &QPushButton::clicked, this, &AddDownloadDialog::onBrowse);
    connect(m_urlEdit, &QLineEdit::textChanged, this, &AddDownloadDialog::onUrlChanged);
    connect(okBtn, &QPushButton::clicked, this, &AddDownloadDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    // Track whether user manually edits the filename so we don't overwrite it
    connect(m_fileNameEdit, &QLineEdit::textEdited, this, [this] {
        m_fileNameEdited = true;
    });

    // Auto-paste URL from clipboard if it looks like a URL
    QString clip = QApplication::clipboard()->text().trimmed();
    if (!clip.isEmpty()) {
        QUrl parsed(clip);
        if (parsed.isValid() &&
            (parsed.scheme() == "http" || parsed.scheme() == "https")) {
            m_urlEdit->setText(clip);
            m_urlEdit->selectAll();
        }
    }
}

std::string AddDownloadDialog::url() const {
    return m_urlEdit->text().trimmed().toStdString();
}

std::filesystem::path AddDownloadDialog::savePath() const {
    return m_savePathEdit->text().toStdString();
}

std::string AddDownloadDialog::fileName() const {
    return m_fileNameEdit->text().trimmed().toStdString();
}

int AddDownloadDialog::segmentCount() const {
    return m_segmentSpin->value();
}

void AddDownloadDialog::setDefaultSegments(int count) {
    m_segmentSpin->setValue(count);
}

void AddDownloadDialog::onBrowse() {
    auto dir = QFileDialog::getExistingDirectory(
        this, "Select Download Directory", m_savePathEdit->text());
    if (!dir.isEmpty())
        m_savePathEdit->setText(dir);
}

void AddDownloadDialog::onUrlChanged() {
    auto text = m_urlEdit->text().trimmed();
    if (text.isEmpty()) return;

    QUrl qurl(text);
    auto path = qurl.path();
    auto fileName = path.mid(path.lastIndexOf('/') + 1);
    // Decode percent-encoded characters for display
    fileName = QUrl::fromPercentEncoding(fileName.toUtf8());
    // Strip query string if it leaked in
    auto q = fileName.indexOf('?');
    if (q != -1) fileName = fileName.left(q);

    if (!fileName.isEmpty()) {
        m_fileNameEdit->setPlaceholderText(fileName);
        // Auto-fill the field itself if the user hasn't typed anything manually
        if (!m_fileNameEdited)
            m_fileNameEdit->setText(fileName);
    }
}

void AddDownloadDialog::onAccept() {
    if (m_urlEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please enter a URL.");
        return;
    }
    auto dirText = m_savePathEdit->text().trimmed();
    if (dirText.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a save directory.");
        return;
    }
    // Create the directory if it doesn't exist yet
    std::error_code ec;
    std::filesystem::create_directories(dirText.toStdString(), ec);
    if (ec) {
        QMessageBox::warning(this, "Error",
            QString("Cannot create directory:\n%1\n\n%2")
                .arg(dirText)
                .arg(QString::fromStdString(ec.message())));
        return;
    }
    accept();
}

} // namespace checkdown
