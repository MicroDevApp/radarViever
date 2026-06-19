#pragma once

#include <QQuickItem>
#include <QMatrix4x4>
#include "TileCache.h"
#include "MapRenderNode.h" // нужен полный тип TileVisual для m_cachedTiles

// QQuickItem с кастомной OpenGL-отрисовкой (QSGRenderNode) внутри.
// Хранит состояние камеры (центр в lon/lat + zoom) и при малом zoom
// плавно "сворачивает" плоскую карту в сферу (см. globeBlend()).
class MapView : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal centerLongitude READ centerLongitude WRITE setCenterLongitude NOTIFY centerChanged)
    Q_PROPERTY(qreal centerLatitude  READ centerLatitude  WRITE setCenterLatitude  NOTIFY centerChanged)
    Q_PROPERTY(qreal zoomLevel       READ zoomLevel       WRITE setZoomLevel       NOTIFY zoomChanged)
    Q_PROPERTY(qreal globeBlend      READ globeBlend                              NOTIFY zoomChanged)

public:
    explicit MapView(QQuickItem *parent = nullptr);
    ~MapView() override;

    qreal centerLongitude() const { return m_lon; }
    qreal centerLatitude()  const { return m_lat; }
    qreal zoomLevel()       const { return m_zoom; }
    qreal globeBlend() const; // 0 = плоская карта, 1 = полностью сфера

    void setCenterLongitude(qreal lon);
    void setCenterLatitude(qreal lat);
    void setZoomLevel(qreal z);

    // Сдвиг центра на dx/dy экранных пикселей (вызывать из MouseArea при драге).
    Q_INVOKABLE void panPixels(qreal dx, qreal dy);
    // Изменение zoom на delta (например +-0.3 на "тик" колеса).
    Q_INVOKABLE void zoomBy(qreal delta);

signals:
    void centerChanged();
    void zoomChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // Вся работа с TileCache (а значит и с сетью) должна происходить
    // только на GUI-потоке. Здесь пересчитываем список видимых тайлов
    // и камеру, кладём в m_cached* и просим перерисовку. updatePaintNode()
    // (он выполняется на render-потоке) после этого просто копирует
    // m_cached* в render node — никаких вызовов TileCache там быть не должно.
    void refreshTiles();

    qreal m_lon = 0.0;
    qreal m_lat = 0.0;
    qreal m_zoom = 2.0;

    TileCache *m_osmCache = nullptr; // цветные тайлы OpenStreetMap
    TileCache *m_demCache = nullptr; // тайлы высот (Terrarium) для relief shading

    QVector<TileVisual> m_cachedTiles;
    QMatrix4x4 m_cachedViewProj;
    qreal m_cachedBlend = 0.0;
    qreal m_cachedCenterU = 0.5;
    qreal m_cachedCenterV = 0.5;

    static constexpr qreal kMinZoom = 0.0;
    static constexpr qreal kMaxZoom = 18.0;
    // Выше этого zoom карта полностью плоская; ниже — начинает сворачиваться в сферу.
    static constexpr qreal kGlobeMaxZoom = 4.0;
};
