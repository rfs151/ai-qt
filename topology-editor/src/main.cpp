#include "MainWindow.h"

#include <QApplication>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TopologyEditor"));
    QApplication::setStyle(QStringLiteral("Fusion"));

#ifdef HAS_EMBEDDED_FONT
    // WASM 环境没有系统字体，加载打包进 qrc 的中文字体子集
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansSC-subset.otf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            QFont f(families.first());
            f.setPixelSize(13);
            QApplication::setFont(f);
        }
    }
#endif

    MainWindow w;
#ifdef Q_OS_WASM
    w.showMaximized();   // 填满浏览器中的容器
#else
    w.show();
#endif
    return app.exec();
}
