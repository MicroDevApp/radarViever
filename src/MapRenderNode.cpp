#include "MapRenderNode.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVector2D>
#include <QVector4D>

namespace {

// --- Вершинный шейдер -----------------------------------------------------
// Каждая вершина — точка unit-сетки [0,1]x[0,1], которая через uTileRect
// отображается в глобальные mercator-координаты, а дальше блендится между
// "плоской" позицией и позицией на сфере (uBlend: 0=плоско, 1=сфера).
const char *kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aUV;

uniform vec4  uTileRect;      // u0,v0,u1,v1 в глобальных mercator-координатах
uniform vec2  uCenterUV;      // текущий центр карты (для пересчёта в локальные координаты)
uniform float uBlend;         // 0 = плоская карта, 1 = сфера
uniform mat4  uViewProj;
uniform float uWorldFlatSize; // ширина плоского мира при zoom=0 (длина окружности сферы)
uniform float uGlobeRadius;

out vec2 vUV;

const float PI = 3.14159265359;

void main()
{
    vec2 guv = mix(uTileRect.xy, uTileRect.zw, aUV);

    // Плоская карта: координаты относительно текущего центра (чтобы не было
    // проблем с точностью float на больших абсолютных координатах).
    vec2 d = guv - uCenterUV;
    vec3 flatPos = vec3(d.x * uWorldFlatSize, -d.y * uWorldFlatSize, 0.0);

    // Сфера: guv.x -> долгота, guv.y (Mercator) -> широта через обратный Mercator.
    float lon  = (guv.x - 0.5) * 2.0 * PI;
    float merN = (0.5 - guv.y) * 2.0 * PI;
    float lat  = atan(sinh(merN));
    vec3 spherePos = uGlobeRadius * vec3(cos(lat) * cos(lon),
                                          sin(lat),
                                          cos(lat) * sin(lon));

    vec3 pos = mix(flatPos, spherePos, uBlend);

    vUV = aUV;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
)GLSL";

// --- Фрагментный шейдер ----------------------------------------------------
// Рельеф без геометрии: высоты из DEM-тайла (Terrarium) используются только
// для расчёта освещённости (hillshading), сама сетка остаётся плоской.
// Декодирование высоты линейно по (r,g,b), поэтому GL_LINEAR-интерполяция
// текстуры и декодирование коммутируют — ошибки интерполяции не возникает.
const char *kFragmentShader = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uOsmTex;
uniform sampler2D uDemTex;
uniform bool  uHasDem;
uniform vec2  uTexel;          // 1/width, 1/height тайла DEM
uniform vec3  uLightDir;       // нормализован
uniform float uReliefStrength; // подбирается на глаз

float decodeHeight(vec2 uv)
{
    vec3 c = texture(uDemTex, uv).rgb * 255.0;
    return c.r * 256.0 + c.g + c.b / 256.0 - 32768.0;
}

void main()
{
    vec4 base = texture(uOsmTex, vUV);
    float shade = 1.0;

    if (uHasDem) {
        float hL = decodeHeight(vUV - vec2(uTexel.x, 0.0));
        float hR = decodeHeight(vUV + vec2(uTexel.x, 0.0));
        float hD = decodeHeight(vUV - vec2(0.0, uTexel.y));
        float hU = decodeHeight(vUV + vec2(0.0, uTexel.y));

        vec3 normal = normalize(vec3((hL - hR) * uReliefStrength,
                                      (hD - hU) * uReliefStrength,
                                      1.0));
        float diff = max(dot(normal, uLightDir), 0.0);
        shade = mix(0.55, 1.25, diff);
    }

    fragColor = vec4(base.rgb * shade, base.a);
}
)GLSL";

} // namespace

MapRenderNode::MapRenderNode()
{
}

MapRenderNode::~MapRenderNode()
{
    for (auto &e : m_osmTextures) delete e.tex;
    for (auto &e : m_demTextures) delete e.tex;
    delete m_program;
}

void MapRenderNode::setCamera(const QMatrix4x4 &viewProj, qreal blendFactor, qreal centerU, qreal centerV)
{
    m_viewProj = viewProj;
    m_blend = blendFactor;
    m_centerU = centerU;
    m_centerV = centerV;
}

void MapRenderNode::setTiles(const QVector<TileVisual> &tiles)
{
    m_tiles = tiles;
}

void MapRenderNode::ensureProgram()
{
    if (m_program)
        return;
    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->link();
}

void MapRenderNode::ensureGeometry()
{
    if (m_vbo.isCreated())
        return;

    const int n = kGridSegments;
    QVector<QVector2D> verts;
    verts.reserve((n + 1) * (n + 1));
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            verts.append(QVector2D(float(i) / n, float(j) / n));

    QVector<quint32> indices;
    indices.reserve(n * n * 6);
    auto idx = [n](int i, int j) { return quint32(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const quint32 a = idx(i, j), b = idx(i + 1, j), c = idx(i, j + 1), d = idx(i + 1, j + 1);
            indices << a << b << c << b << d << c;
        }
    }
    m_indexCount = indices.size();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(verts.constData(), verts.size() * int(sizeof(QVector2D)));
    m_vbo.release();

    m_ibo.create();
    m_ibo.bind();
    m_ibo.allocate(indices.constData(), indices.size() * int(sizeof(quint32)));
    m_ibo.release();
}

QOpenGLTexture *MapRenderNode::textureFor(QHash<quint64, TexEntry> &cache, quint64 key, const QImage &img)
{
    auto it = cache.find(key);
    if (img.isNull())
        return it != cache.end() ? it->tex : nullptr;

    if (it != cache.end() && it->cacheKey == img.cacheKey())
        return it->tex; // картинка не менялась — текстура уже актуальна

    auto *tex = new QOpenGLTexture(img.convertToFormat(QImage::Format_RGBA8888));
    tex->setMinificationFilter(QOpenGLTexture::Linear);
    tex->setMagnificationFilter(QOpenGLTexture::Linear);
    tex->setWrapMode(QOpenGLTexture::ClampToEdge);

    if (it != cache.end()) {
        delete it->tex;
        it->tex = tex;
        it->cacheKey = img.cacheKey();
    } else {
        cache.insert(key, {tex, img.cacheKey()});
    }
    return tex;
}

void MapRenderNode::render(const RenderState *)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;
    QOpenGLFunctions *f = ctx->functions();

    ensureProgram();
    ensureGeometry();
    if (!m_program->isLinked())
        return;

    f->glEnable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_LESS);
    f->glDisable(GL_BLEND);
    f->glEnable(GL_CULL_FACE);
    f->glCullFace(GL_BACK);

    m_program->bind();
    m_program->setUniformValue("uViewProj", m_viewProj);
    m_program->setUniformValue("uBlend", float(m_blend));
    m_program->setUniformValue("uCenterUV", QVector2D(float(m_centerU), float(m_centerV)));
    m_program->setUniformValue("uWorldFlatSize", float(2.0 * M_PI * kGlobeRadius));
    m_program->setUniformValue("uGlobeRadius", float(kGlobeRadius));
    m_program->setUniformValue("uLightDir", QVector3D(0.4f, 0.6f, 0.7f).normalized());
    m_program->setUniformValue("uReliefStrength", 0.02f); // подобрать под вкус
    m_program->setUniformValue("uOsmTex", 0);
    m_program->setUniformValue("uDemTex", 1);

    m_vbo.bind();
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, sizeof(QVector2D));
    m_ibo.bind();

    QSet<quint64> needed;
    for (const TileVisual &t : m_tiles) {
        needed.insert(t.key);

        QOpenGLTexture *osmTex = textureFor(m_osmTextures, t.key, t.osmImage);
        if (!osmTex)
            continue; // текстуры пока нет ни в новом, ни в старом кадре

        f->glActiveTexture(GL_TEXTURE0);
        osmTex->bind();

        QOpenGLTexture *demTex = textureFor(m_demTextures, t.key, t.demImage);
        const bool hasDem = (demTex != nullptr);
        m_program->setUniformValue("uHasDem", hasDem);
        if (hasDem) {
            f->glActiveTexture(GL_TEXTURE1);
            demTex->bind();
            m_program->setUniformValue("uTexel", QVector2D(1.0f / demTex->width(), 1.0f / demTex->height()));
        }

        m_program->setUniformValue("uTileRect", QVector4D(float(t.uvRect.left()), float(t.uvRect.top()),
                                                            float(t.uvRect.right()), float(t.uvRect.bottom())));

        f->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }

    m_ibo.release();
    m_vbo.release();
    m_program->disableAttributeArray(0);
    m_program->release();

    // Выгружаем текстуры тайлов, которые больше не видны.
    for (auto it = m_osmTextures.begin(); it != m_osmTextures.end();) {
        if (!needed.contains(it.key())) { delete it->tex; it = m_osmTextures.erase(it); }
        else ++it;
    }
    for (auto it = m_demTextures.begin(); it != m_demTextures.end();) {
        if (!needed.contains(it.key())) { delete it->tex; it = m_demTextures.erase(it); }
        else ++it;
    }

    f->glDisable(GL_CULL_FACE);
    f->glDisable(GL_DEPTH_TEST);
}

QSGRenderNode::StateFlags MapRenderNode::changedStates() const
{
    return StateFlags(DepthState | StencilState | CullState | BlendState);
}

QSGRenderNode::RenderingFlags MapRenderNode::flags() const
{
    return RenderingFlags(DepthAwareRendering);
}
