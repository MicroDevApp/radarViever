#include "MapView.h"
#include "MapRenderNode.h"

#include <QtMath>
#include <QVector3D>
#include <QMatrix4x4>
#include <QQuickWindow>        // FIX: нужен для setGraphicsApi если используешь Qt6
#include <QSGRendererInterface>
#include <cmath>

namespace {
constexpr double kWorldFlatSize = 2.0 * M_PI;
}

MapView::MapView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::NoButton);

    // FIX: явно просим Qt6 использовать OpenGL, а не Vulkan/Metal/D3D.
    // Без этого на некоторых платформах (macOS, Windows с Qt6) QSGRenderNode
    // получает не-OpenGL контекст и ctx->functions() возвращает мусор.
    // Вызов должен происходить до создания QQuickWindow, поэтому здесь он
    // безопасен только если MapView создаётся до показа окна.
    // Надёжнее перенести его в main() до создания QGuiApplication:
    //   QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    // Оставляем здесь как запасной вариант — повторный вызов безвреден.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    m_osmCache = new TileCache(
        "osm",
        "https://tile.openstreetmap.org/%1/%2/%3.png",
        "MapGlobeDemo/1.0 (+your-contact-here)",
        this);

    m_demCache = new TileCache(
        "dem",
        "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png",
        "MapGlobeDemo/1.0 (+your-contact-here)",
        this);

    connect(m_osmCache, &TileCache::tileReady, this, [this](int, int, int) { refreshTiles(); });
    connect(m_demCache, &TileCache::tileReady, this, [this](int, int, int) { refreshTiles(); });
}

MapView::~MapView() = default;

qreal MapView::globeBlend() const
{
    const double t = 1.0 - qBound(0.0, m_zoom / kGlobeMaxZoom, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void MapView::setCenterLongitude(qreal lon)
{
    while (lon >  180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;
    if (qFuzzyCompare(lon + 1.0, m_lon + 1.0)) return;
    m_lon = lon;
    emit centerChanged();
    refreshTiles();
}

void MapView::setCenterLatitude(qreal lat)
{
    lat = qBound(-85.0, lat, 85.0);
    if (qFuzzyCompare(lat + 1.0, m_lat + 1.0)) return;
    m_lat = lat;
    emit centerChanged();
    refreshTiles();
}

void MapView::setZoomLevel(qreal z)
{
    z = qBound(kMinZoom, z, kMaxZoom);
    if (qFuzzyCompare(z + 1.0, m_zoom + 1.0)) return;
    m_zoom = z;
    emit zoomChanged();
    refreshTiles();
}

void MapView::panPixels(qreal dx, qreal dy)
{
    const double n = std::pow(2.0, m_zoom);
    const double degPerPixelLon = 360.0 / (256.0 * n);
    const double latRad = qDegreesToRadians(m_lat);
    const double degPerPixelLat = degPerPixelLon / qMax(0.15, std::cos(latRad));

    setCenterLongitude(m_lon - dx * degPerPixelLon);
    setCenterLatitude(m_lat  + dy * degPerPixelLat);
}

void MapView::zoomBy(qreal delta)
{
    setZoomLevel(m_zoom + delta);
}

void MapView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    refreshTiles();
}

QSGNode *MapView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Выполняется на render-потоке (sync-фаза).
    // Только читаем m_cached* — никаких обращений к TileCache/сети.
    auto *node = static_cast<MapRenderNode *>(oldNode);
    if (!node)
        node = new MapRenderNode();

    node->setCamera(m_cachedViewProj, m_cachedBlend, m_cachedCenterU, m_cachedCenterV);
    node->setTiles(m_cachedTiles);
    return node;
}

void MapView::refreshTiles()
{
    const qreal w = width();
    const qreal h = height();
    if (w <= 0 || h <= 0)
        return;

    const int tileZ       = int(std::floor(qBound(kMinZoom, m_zoom, kMaxZoom)));
    const int tilesPerSide = 1 << tileZ;

    const double centerU = (m_lon + 180.0) / 360.0;
    const double latRad  = qDegreesToRadians(qBound(-85.0, m_lat, 85.0));
    const double centerV = (1.0 - std::log(std::tan(M_PI / 4.0 + latRad / 2.0)) / M_PI) / 2.0;

    int x0, x1, y0, y1;
    if (tileZ <= 3) {
        x0 = 0; x1 = tilesPerSide - 1;
        y0 = 0; y1 = tilesPerSide - 1;
    } else {
        const double pixelsPerTile = 256.0;
        const int tilesX = int(std::ceil(w / pixelsPerTile)) + 2;
        const int tilesY = int(std::ceil(h / pixelsPerTile)) + 2;
        const int centerTileX = int(std::floor(centerU * tilesPerSide));
        const int centerTileY = int(std::floor(centerV * tilesPerSide));
        x0 = centerTileX - tilesX / 2;
        x1 = centerTileX + tilesX / 2;
        y0 = qMax(0, centerTileY - tilesY / 2);
        y1 = qMin(tilesPerSide - 1, centerTileY + tilesY / 2);
    }

    QVector<TileVisual> tiles;
    tiles.reserve((x1 - x0 + 1) * (y1 - y0 + 1));
    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const int wrappedX = ((tx % tilesPerSide) + tilesPerSide) % tilesPerSide;

            QImage osmImg, demImg;
            if (!m_osmCache->tile(tileZ, wrappedX, ty, osmImg))
                continue;
            m_demCache->tile(tileZ, wrappedX, ty, demImg);

            TileVisual tv;
            tv.key = TileCache::key(tileZ, wrappedX, ty);
            const double u0 = double(tx)     / tilesPerSide;
            const double u1 = double(tx + 1) / tilesPerSide;
            const double v0 = double(ty)     / tilesPerSide;
            const double v1 = double(ty + 1) / tilesPerSide;
            tv.uvRect   = QRectF(u0, v0, u1 - u0, v1 - v0);
            tv.osmImage = osmImg;
            tv.demImage = demImg;
            tiles.append(tv);
        }
    }

    // --- Камера ---
    const double t     = globeBlend();
    const double hFlat = (kWorldFlatSize / std::pow(2.0, m_zoom)) * 1.3;

    const QVector3D flatEye(0.0f, 0.0f, float(hFlat));
    const QVector3D flatCenter(0.0f, 0.0f, 0.0f);
    const QVector3D flatUp(0.0f, 1.0f, 0.0f);

    const double lonRad = qDegreesToRadians(m_lon);
    const QVector3D dir(float(std::cos(latRad) * std::cos(lonRad)),
                        float(std::sin(latRad)),
                        float(std::cos(latRad) * std::sin(lonRad)));
    const double dSphere   = 1.3 + 3.0 * t;
    const QVector3D sphereEye    = dir * float(dSphere);
    const QVector3D sphereCenter(0.0f, 0.0f, 0.0f);
    const QVector3D worldUp(0.0f, 1.0f, 0.0f);
    const QVector3D sphereUp =
        (std::abs(QVector3D::dotProduct(dir, worldUp)) > 0.99f)
            ? QVector3D(0.0f, 0.0f, 1.0f) : worldUp;

    auto lerp = [](const QVector3D &a, const QVector3D &b, float f) {
        return a * (1.0f - f) + b * f;
    };
    const QVector3D eye    = lerp(flatEye,    sphereEye,    float(t));
    const QVector3D center = lerp(flatCenter, sphereCenter, float(t));
    const QVector3D up     = lerp(flatUp,     sphereUp,     float(t));

    QMatrix4x4 view;
    view.lookAt(eye, center, up);
    QMatrix4x4 proj;
    proj.perspective(45.0f, float(w / qMax(1.0, double(h))), 0.001f, 100.0f);

    m_cachedTiles    = tiles;
    m_cachedViewProj = proj * view;
    m_cachedBlend    = t;
    m_cachedCenterU  = centerU;
    m_cachedCenterV  = centerV;

    qDebug() << "refreshTiles: tiles=" << tiles.size() << "blend=" << t << "zoom=" << m_zoom;

    update();
}
