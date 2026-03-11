#include "DownloadTableModel.h"

#include <format>
#include <cmath>

namespace checkdown {

DownloadTableModel::DownloadTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int DownloadTableModel::rowCount(const QModelIndex& /*parent*/) const {
    return static_cast<int>(m_rows.size());
}

int DownloadTableModel::columnCount(const QModelIndex& /*parent*/) const {
    return ColCount;
}

QVariant DownloadTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    auto& row = m_rows[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColId:       return row.id;
            case ColFileName: return row.isYtdlp
                                  ? "[yt-dlp] " + row.fileName
                                  : row.fileName;
            case ColSize:     return (row.totalSize > 0)
                                  ? formatBytes(row.totalSize) : QString("—");
            case ColProgress: {
                bool indeterminate = (row.totalSize <= 0 &&
                                      row.state == DownloadState::Downloading);
                if (indeterminate) return QString("Downloading\u2026");
                if (row.state == DownloadState::Completed) return QString("Done");
                return QString::number(row.progress, 'f', 1) + "%";
            }
            case ColSpeed:    return (row.state == DownloadState::Downloading)
                                  ? formatSpeed(row.speed) : QString{};
            case ColEta:      return (row.state == DownloadState::Downloading && row.etaSeconds >= 0)
                                  ? formatEta(row.etaSeconds) : QString{};
            case ColStatus:   return QString::fromUtf8(toString(row.state));
            case ColUrl:      return row.url;
        }
    }

    // Tooltip on filename: show full URL
    if (role == Qt::ToolTipRole && index.column() == ColFileName)
        return row.url;

    // UserRole: progress percentage for delegate; -1.0 signals indeterminate
    if (role == Qt::UserRole && index.column() == ColProgress) {
        bool indeterminate = (row.totalSize <= 0 &&
                              row.state == DownloadState::Downloading);
        return indeterminate ? -1.0 : row.progress;
    }

    // UserRole+1: DownloadState integer for delegate color selection
    if (role == Qt::UserRole + 1 && index.column() == ColProgress) {
        return static_cast<int>(row.state);
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case ColId:
            case ColSize:
            case ColProgress:
            case ColSpeed:
            case ColEta:
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return {};
}

QVariant DownloadTableModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
        case ColId:       return "#";
        case ColFileName: return "File Name";
        case ColSize:     return "Size";
        case ColProgress: return "Progress";
        case ColSpeed:    return "Speed";
        case ColEta:      return "ETA";
        case ColStatus:   return "Status";
        case ColUrl:      return "URL";
    }
    return {};
}

void DownloadTableModel::updateDownload(const TaskProgress& progress) {
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[i].id == progress.taskId) {
            auto& row = m_rows[i];
            row.downloadedBytes = progress.downloadedBytes;
            row.state           = progress.state;
            row.speed           = progress.speedBytesPerSec;
            row.etaSeconds      = progress.etaSeconds;

            if (!progress.fileName.empty())
                row.fileName = QString::fromStdString(progress.fileName);

            if (progress.totalBytes > 0) {
                row.totalSize = progress.totalBytes;
                row.progress  = (static_cast<double>(progress.downloadedBytes) /
                                 static_cast<double>(progress.totalBytes)) * 100.0;
            } else if (progress.state == DownloadState::Completed) {
                row.progress = 100.0;
            }

            // Clear speed/ETA when not actively downloading
            if (progress.state != DownloadState::Downloading) {
                row.speed      = 0.0;
                row.etaSeconds = -1.0;
            }

            emit dataChanged(index(i, 0), index(i, ColCount - 1));
            return;
        }
    }
}

void DownloadTableModel::setDownloads(std::vector<DownloadInfo> downloads) {
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(downloads.size());

    for (auto& d : downloads) {
        Row row;
        row.id        = d.id;
        row.fileName  = QString::fromStdString(d.fileName);
        row.savePath  = QString::fromStdString(d.savePath.string());
        row.totalSize = d.totalSize;
        row.state     = d.state;
        row.url       = QString::fromStdString(d.url);
        row.isYtdlp   = d.isYtdlp;

        // For regular tasks, sum segment bytes. For yt-dlp tasks (no segments), use the field directly.
        int64_t dl = d.downloadedBytes;
        if (!d.segments.empty()) {
            dl = 0;
            for (auto& s : d.segments) dl += s.downloadedBytes;
        }
        row.downloadedBytes = dl;
        if (d.totalSize > 0)
            row.progress = static_cast<double>(dl) / static_cast<double>(d.totalSize) * 100.0;
        else if (d.state == DownloadState::Completed)
            row.progress = 100.0;

        m_rows.push_back(std::move(row));
    }
    endResetModel();
}

int DownloadTableModel::downloadIdAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;
    return m_rows[row].id;
}

DownloadState DownloadTableModel::stateAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return DownloadState::Queued;
    return m_rows[row].state;
}

QString DownloadTableModel::savePathAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};
    return m_rows[row].savePath;
}

bool DownloadTableModel::isYtdlpAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return false;
    return m_rows[row].isYtdlp;
}

int DownloadTableModel::activeCount() const {
    int n = 0;
    for (auto& row : m_rows)
        if (row.state == DownloadState::Downloading) ++n;
    return n;
}

double DownloadTableModel::totalActiveSpeed() const {
    double total = 0.0;
    for (auto& row : m_rows)
        if (row.state == DownloadState::Downloading) total += row.speed;
    return total;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
QString DownloadTableModel::formatBytes(int64_t bytes) {
    if (bytes < 0) return "?";

    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIdx = 0;
    double val = static_cast<double>(bytes);

    while (val >= 1024.0 && unitIdx < 4) {
        val /= 1024.0;
        ++unitIdx;
    }

    if (unitIdx == 0)
        return QString::number(bytes) + " B";
    return QString::number(val, 'f', 2) + " " + units[unitIdx];
}

QString DownloadTableModel::formatSpeed(double bytesPerSec) {
    if (bytesPerSec <= 0.0) return {};
    return formatBytes(static_cast<int64_t>(bytesPerSec)) + "/s";
}

QString DownloadTableModel::formatEta(double seconds) {
    if (seconds < 0) return {};
    auto s = static_cast<int64_t>(std::ceil(seconds));
    if (s < 60)
        return QString("%1s").arg(s);
    if (s < 3600)
        return QString("%1m %2s").arg(s / 60).arg(s % 60);
    return QString("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
}

} // namespace checkdown
