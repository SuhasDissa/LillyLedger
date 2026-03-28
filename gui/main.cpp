#include "mainwindow.h"
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QStyleFactory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));

    const QStringList preferredFamilies = {"Plus Jakarta Sans", "Work Sans", "Noto Sans",
                                           "DejaVu Sans", "Sans Serif"};
    QFont uiFont;
    const QStringList availableFamilies = QFontDatabase::families();
    for (const QString &family : preferredFamilies) {
        if (availableFamilies.contains(family, Qt::CaseInsensitive)) {
            uiFont = QFont(family);
            break;
        }
    }
    if (uiFont.family().isEmpty()) {
        uiFont = app.font();
    }
    uiFont.setPointSize(11);
    app.setFont(uiFont);

    MainWindow window;
    window.show();
    return app.exec();
}
