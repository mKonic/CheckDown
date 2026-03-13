#include "ProgressDelegate.h"
#include "../core/Types.h"

#include <QPainter>
#include <QPainterPath>
#include <QApplication>

namespace checkdown {

static QColor stateColor(DownloadState state) {
    switch (state) {
        case DownloadState::Completed:  return {166, 227, 161};  // green (catppuccin)
        case DownloadState::Failed:     return {243, 139, 168};  // red
        case DownloadState::Paused:     return {249, 226, 175};  // yellow
        case DownloadState::Cancelled:  return {108, 112, 134};  // overlay0
        case DownloadState::Queued:     return {88,  91,  112};  // surface2
        default:                        return {137, 180, 250};  // blue
    }
}

static QColor stateTrackColor(DownloadState state) {
    switch (state) {
        case DownloadState::Completed:  return {166, 227, 161, 30};
        case DownloadState::Failed:     return {243, 139, 168, 30};
        case DownloadState::Paused:     return {249, 226, 175, 30};
        case DownloadState::Cancelled:  return {108, 112, 134, 30};
        case DownloadState::Queued:     return {88,  91,  112, 30};
        default:                        return {137, 180, 250, 30};
    }
}

void ProgressDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    double progress = index.data(Qt::UserRole).toDouble();
    auto state = static_cast<DownloadState>(index.data(Qt::UserRole + 1).toInt());
    bool indeterminate = (progress < 0.0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Selection background
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());

    // Bar geometry
    QRectF barRect = QRectF(option.rect).adjusted(6, 5, -6, -5);
    double radius = barRect.height() / 2.0;

    // Track (background)
    QPainterPath trackPath;
    trackPath.addRoundedRect(barRect, radius, radius);
    painter->fillPath(trackPath, stateTrackColor(state));

    // Fill
    if (!indeterminate && progress > 0.0) {
        double fillWidth = barRect.width() * std::min(progress, 100.0) / 100.0;
        if (fillWidth > 1.0) {
            QRectF fillRect(barRect.left(), barRect.top(), fillWidth, barRect.height());

            QPainterPath fillPath;
            fillPath.addRoundedRect(fillRect, radius, radius);
            // Intersect with track so right edge is clipped when not 100%
            fillPath = fillPath.intersected(trackPath);

            QColor fill = stateColor(state);
            painter->fillPath(fillPath, fill);
        }
    } else if (indeterminate) {
        // Pulsing bar animation placeholder — just fill 40% centered
        double w = barRect.width() * 0.4;
        QRectF fillRect(barRect.center().x() - w / 2.0, barRect.top(), w, barRect.height());
        QPainterPath fillPath;
        fillPath.addRoundedRect(fillRect, radius, radius);
        fillPath = fillPath.intersected(trackPath);
        QColor fill = stateColor(state);
        fill.setAlpha(140);
        painter->fillPath(fillPath, fill);
    }

    // Text overlay
    QString text;
    if (indeterminate)
        text = QString("Downloading\u2026");
    else if (state == DownloadState::Completed)
        text = "Done";
    else
        text = QString::number(progress, 'f', 1) + "%";

    QFont font = option.font;
    font.setPointSizeF(font.pointSizeF() - 0.5);
    font.setWeight(QFont::DemiBold);
    painter->setFont(font);

    // Text color: dark on filled bar, light on empty track
    QColor textColor;
    if (state == DownloadState::Completed || (progress > 55.0 && !indeterminate)) {
        textColor = QColor(30, 30, 46);  // dark text on filled bar
    } else {
        textColor = stateColor(state);   // colored text on dark track
    }
    painter->setPen(textColor);
    painter->drawText(barRect, Qt::AlignCenter, text);

    painter->restore();
}

QSize ProgressDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                  const QModelIndex& /*index*/) const {
    return {140, 28};
}

} // namespace checkdown
