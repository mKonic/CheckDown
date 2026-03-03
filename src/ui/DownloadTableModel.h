#pragma once

#include "../core/Types.h"

#include <QAbstractTableModel>
#include <vector>

namespace checkdown {

class DownloadTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColId = 0,
        ColFileName,
        ColSize,
        ColProgress,
        ColSpeed,
        ColStatus,
        ColUrl,
        ColCount
    };

    explicit DownloadTableModel(QObject* parent = nullptr);

    // QAbstractTableModel interface
    int      rowCount(const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    /// Called from UI thread to refresh a single download.
    void updateDownload(const TaskProgress& progress);

    /// Bulk-set all downloads (e.g. on startup load).
    void setDownloads(std::vector<DownloadInfo> downloads);

    /// Get download ID at a given row.
    [[nodiscard]] int downloadIdAt(int row) const;

    /// Get state at a given row.
    [[nodiscard]] DownloadState stateAt(int row) const;

private:
    static QString formatBytes(int64_t bytes);
    static QString formatSpeed(double bytesPerSec);

    struct Row {
        int           id             = 0;
        QString       fileName;
        int64_t       totalSize      = -1;
        int64_t       downloadedBytes= 0;
        double        progress       = 0.0;    // 0-100
        double        speed          = 0.0;
        DownloadState state          = DownloadState::Queued;
        QString       url;
    };

    std::vector<Row> m_rows;
};

} // namespace checkdown
