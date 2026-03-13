#pragma once

#include "../core/Types.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
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
        ColEta,
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

    /// Get save path at a given row (for opening completed files).
    [[nodiscard]] QString savePathAt(int row) const;

    /// Returns true if the row is a yt-dlp task (not a segmented HTTP task).
    [[nodiscard]] bool isYtdlpAt(int row) const;

    /// Aggregate helpers for status bar.
    [[nodiscard]] int    activeCount() const;
    [[nodiscard]] double totalActiveSpeed() const;

    static QString formatBytes(int64_t bytes);

private:
    static QString formatSpeed(double bytesPerSec);
    static QString formatEta(double seconds);

    struct Row {
        int           id             = 0;
        QString       fileName;
        QString       savePath;
        int64_t       totalSize      = -1;
        int64_t       downloadedBytes= 0;
        double        progress       = 0.0;    // 0-100
        double        speed          = 0.0;
        double        etaSeconds     = -1.0;
        DownloadState state          = DownloadState::Queued;
        QString       url;
        bool          isYtdlp        = false;
    };

    std::vector<Row> m_rows;
};

} // namespace checkdown
