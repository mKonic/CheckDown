#include "ProgressDelegate.h"

#include <QPainter>
#include <QApplication>
#include <QStyleOptionProgressBar>

namespace checkdown {

void ProgressDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    // Expect Qt::UserRole to hold the progress percentage (0.0 - 100.0)
    double progress = index.data(Qt::UserRole).toDouble();

    QStyleOptionProgressBar progressBar;
    progressBar.rect    = option.rect.adjusted(2, 2, -2, -2);
    progressBar.minimum = 0;
    progressBar.maximum = 100;
    progressBar.progress = static_cast<int>(progress);
    progressBar.text     = QString::number(progress, 'f', 1) + "%";
    progressBar.textVisible = true;

    // Draw background for selected rows
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    QApplication::style()->drawControl(QStyle::CE_ProgressBar,
                                        &progressBar, painter);
}

QSize ProgressDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                  const QModelIndex& /*index*/) const {
    return {100, 24};
}

} // namespace checkdown
