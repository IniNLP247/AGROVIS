#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "AppController.h"
#include "CaptureListModel.h"
#include "ViewportImageProvider.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AGROVIS"));
    app.setOrganizationName(QStringLiteral("AGROVIS"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // Basic style guarantees consistent custom dark styling on Ubuntu, Docker, and Windows.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    auto* captureModel = new agrovis::app::CaptureListModel(&app);
    auto* imageProvider = new agrovis::app::ViewportImageProvider();  // Owned by QQmlEngine

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.addImageProvider(QStringLiteral("viewport"), imageProvider);

    agrovis::app::AppController controller(captureModel, imageProvider);
    if (argc > 1) {
        controller.loadFolder(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    }
    engine.rootContext()->setContextProperty(QStringLiteral("appCtrl"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("captureModel"), captureModel);

    const QUrl mainUrl(QStringLiteral("qrc:/qt/qml/Agrovis/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [mainUrl](QObject* obj, const QUrl& objUrl) {
            if (!obj && mainUrl == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(mainUrl);
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
