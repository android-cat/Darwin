#include "MainWindow.h"
#include "widgets/SplashWidget.h"
#include <QApplication>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QDir>
#include "common/ThemeManager.h"

#ifdef Q_OS_WIN
#include <objbase.h>

// NVIDIA Optimus: 外部GPU（高性能GPU）を優先して使用する
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement = 1; }
// AMD PowerXpress: 外部GPU（高性能GPU）を優先して使用する
extern "C" { __declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1; }
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Early COM initialization (Apartment Threaded) required by some VST3 plugins / UI frameworks
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    // Force OpenGL backend to prevent DirectComposition/WarpPal conflicts with WinUI 3 plugins
    qputenv("QSG_RHI_BACKEND", "opengl");
#endif

    QApplication app(argc, argv);
    Darwin::ThemeManager::instance().initialize();

    // Projectフォルダが存在しなければ作成
    {
        QString projectDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                             + "/Darwin/Projects";
        QDir dir(projectDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
    }

    // Set application-wide font (fallback to standard system fonts for now)
    // In a real scenario, we might want to load fonts from resources if available.
    // uidesign.html uses "Helvetica Neue", "Inter", "Arial"
    QFont font("Arial"); 
    font.setStyleHint(QFont::SansSerif);
    app.setFont(font);

    MainWindow w;
    // w.show(); はSplashWidget終了後に呼ぶためここでは呼ばない

    SplashWidget* splash = new SplashWidget();
    splash->show();

    // スプラッシュのアニメーションが終わったら MainWindow を表示
    QObject::connect(splash, &SplashWidget::finished, &w, &QWidget::show);

    int result = app.exec();

#ifdef Q_OS_WIN
    CoUninitialize();
#endif

    return result;
}
