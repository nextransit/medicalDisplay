#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include <QDebug>
#include "imagerenderer.h"
#include "dicomloader.h"

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setSwapInterval(1);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName("AI Medical Display");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("MedicalDisplay");

    QQuickStyle::setStyle("Material");

    // 创建图像渲染器 + DICOM 加载器
    auto *renderer = new ImageRenderer();
    auto *dicomLoader = new DicomLoader();

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("med"), renderer);
    engine.addImageProvider(QStringLiteral("dicom"), dicomLoader);
    engine.rootContext()->setContextProperty("Renderer", renderer);
    engine.rootContext()->setContextProperty("Dicom", dicomLoader);
    engine.rootContext()->setContextProperty("appVersion", "2.0.0");

    const QUrl url(QStringLiteral("qrc:/qml/MainWindow.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    qDebug() << "AI Medical Display v2.0 启动成功";
    return app.exec();
}
