#include "ProgressDelegate.h"
#include "../core/Types.h"

#include <QPainter>
#include <QApplication>
#include <QStyleOptionProgressBar>

namespace checkdown {

// Progress bar fill colors per state
static QColor stateColor(DownloadState state) {
    switch (state) {
        case DownloadState::Completed:  return {52,  168, 83};   // green
        case DownloadState::Failed:     return {220, 53,  69};   // red
        case DownloadState::Paused:     return {255, 163, 0};    // amber
        case DownloadState::Cancelled:  return {150, 150, 150};  // gray
        default:                        return {13,  110, 253};  // blue
    }
}

void ProgressDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    // Qt::UserRole  → progress percentage (0-100), or -1 for indeterminate
    // Qt::UserRole+1 → DownloadState (int)
    double progress = index.data(Qt::UserRole).toDouble();
    auto state = static_cast<DownloadState>(index.data(Qt::UserRole + 1).toInt());
    bool indeterminate = (progress < 0.0);

    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());

    QStyleOptionProgressBar pb;
    pb.rect     = option.rect.adjusted(4, 3, -4, -3);
    pb.minimum  = 0;
    pb.maximum  = indeterminate ? 0 : 100;   // min==max == indeterminate for Qt styles
    pb.progress = indeterminate ? 0 : static_cast<int>(progress);
    pb.text     = indeterminate ? QString("Downloading\u2026")
                                : (state == DownloadState::Completed
                                       ? "Done"
                                       : QString::number(progress, 'f', 1) + "%");
    pb.textVisible = true;

    // Re-colour the progress bar highlight via palette
    QPalette pal = option.palette;
    pal.setColor(QPalette::Highlight, stateColor(state));
    pb.palette = pal;

    QApplication::style()->drawControl(QStyle::CE_ProgressBar, &pb, painter);
}

QSize ProgressDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                  const QModelIndex& /*index*/) const {
    return {120, 26};
}

} // namespace checkdown
