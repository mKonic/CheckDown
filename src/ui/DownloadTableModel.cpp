#include "DownloadTableModel.h"

#include <format>

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
            case ColFileName: return row.fileName;
            case ColSize:     return (row.totalSize > 0)
                                  ? formatBytes(row.totalSize) : QString("Unknown");
            case ColProgress: return QString::number(row.progress, 'f', 1) + "%";
            case ColSpeed:    return (row.state == DownloadState::Downloading)
                                  ? formatSpeed(row.speed) : QString{};
            case ColStatus:   return QString::fromUtf8(toString(row.state));
            case ColUrl:      return row.url;
        }
    }

    // UserRole on the progress column stores the raw percentage for the delegate
    if (role == Qt::UserRole && index.column() == ColProgress) {
        return row.progress;
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case ColId:
            case ColSize:
            case ColProgress:
            case ColSpeed:
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

            if (progress.totalBytes > 0) {
                row.totalSize = progress.totalBytes;
                row.progress  = (static_cast<double>(progress.downloadedBytes) /
                                 static_cast<double>(progress.totalBytes)) * 100.0;
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
        row.totalSize = d.totalSize;
        row.state     = d.state;
        row.url       = QString::fromStdString(d.url);

        int64_t dl = 0;
        for (auto& s : d.segments)
            dl += s.downloadedBytes;
        row.downloadedBytes = dl;
        row.progress = (d.totalSize > 0)
            ? (static_cast<double>(dl) / static_cast<double>(d.totalSize) * 100.0)
            : 0.0;

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

} // namespace checkdown
