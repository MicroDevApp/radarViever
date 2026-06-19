#pragma once

#include <QSGRenderNode>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QMatrix4x4>
#include <QRectF>

// Один видимый тайл для текущего кадра.
// osmImage/demImage могут быть null, если тайл уже был закачан на GPU
// в предыдущем кадре (см. MapRenderNode::textureFor) — это просто
// "продолжай рисовать этот тайл, текстура уже у тебя есть".
struct TileVisual
{
    quint64 key = 0;
    QRectF uvRect;     // глобальные нормализованные mercator-координаты [0,1]x[0,1]
    QImage osmImage;
    QImage demImage;
};

// QSGRenderNode с ручной OpenGL-отрисовкой тайлов. Каждый тайл рисуется
// одной и той же общей сеткой (unit grid), а конкретный кусок планеты
// задаётся uniform'ом uTileRect — это избавляет от пересборки геометрии
// при каждом изменении набора видимых тайлов.
class MapRenderNode : public QSGRenderNode
{
public:
    MapRenderNode();
    ~MapRenderNode() override;

    void setCamera(const QMatrix4x4 &viewProj, qreal blendFactor, qreal centerU, qreal centerV);
    void setTiles(const QVector<TileVisual> &tiles);

    void render(const RenderState *state) override;
    StateFlags changedStates() const override;
    RenderingFlags flags() const override;

private:
    struct TexEntry {
        QOpenGLTexture *tex = nullptr;
        qint64 cacheKey = -1;
    };

    void ensureGeometry();
    void ensureProgram();
    QOpenGLTexture *textureFor(QHash<quint64, TexEntry> &cache, quint64 key, const QImage &img);

    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_ibo{QOpenGLBuffer::IndexBuffer};
    int m_indexCount = 0;

    QHash<quint64, TexEntry> m_osmTextures;
    QHash<quint64, TexEntry> m_demTextures;

    QVector<TileVisual> m_tiles;
    QMatrix4x4 m_viewProj;
    qreal m_blend = 0.0;
    qreal m_centerU = 0.5;
    qreal m_centerV = 0.5;

    static constexpr int kGridSegments = 16; // 17x17 вершин на тайл
public:
    static constexpr double kGlobeRadius = 1.0;
};
