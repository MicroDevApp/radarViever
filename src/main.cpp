#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QStandardPaths>
#include "MapView.h"

int main(int argc, char *argv[])
{
    // Принудительно используем классический OpenGL-backend Qt Quick
    // (по умолчанию в Qt6 на некоторых платформах используется RHI
    // через Direct3D/Metal/Vulkan, а QSGRenderNode здесь написан
    // в терминах прямых вызовов OpenGL).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);


    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);

    qmlRegisterType<MapView>("MapGlobeApp", 1, 0, "MapView");

    qDebug() << "AppData path:" << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                      &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
