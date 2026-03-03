#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include "ui/MainWindow.h"
#include "core/HttpClient.h"
#include "core/Types.h"

// Register custom types for cross-thread signal/slot delivery
Q_DECLARE_METATYPE(checkdown::TaskProgress)

/// Generate a simple app icon programmatically (down-arrow in a circle).
/// Replace this with a QRC-embedded icon when a proper .ico is available.
static QIcon createAppIcon() {
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Circle background — blue
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x33, 0x99, 0xFF));
    p.drawEllipse(2, 2, 60, 60);

    // Down arrow — white
    p.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(32, 14, 32, 44);     // vertical
    p.drawLine(32, 44, 18, 30);     // left branch
    p.drawLine(32, 44, 46, 30);     // right branch

    // Bottom bar
    p.drawLine(16, 52, 48, 52);

    p.end();
    return QIcon(pix);
}

int main(int argc, char* argv[]) {
    checkdown::HttpClient::globalInit();

    QApplication app(argc, argv);
    app.setApplicationName("CheckDown");
    app.setOrganizationName("CheckDown");
    app.setApplicationVersion("1.0.0");

    // Keep app running when the window is closed (tray mode)
    app.setQuitOnLastWindowClosed(false);

    // Set app icon (used by tray icon, taskbar, window title)
    QIcon appIcon = createAppIcon();
    app.setWindowIcon(appIcon);

    qRegisterMetaType<checkdown::TaskProgress>("checkdown::TaskProgress");

    checkdown::MainWindow window;

    // Check if launched with --minimized (e.g., from "Start with Windows")
    bool startMinimized = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--minimized") {
            startMinimized = true;
            break;
        }
    }

    if (!startMinimized) {
        window.show();
    }

    int ret = app.exec();

    checkdown::HttpClient::globalCleanup();
    return ret;
}
