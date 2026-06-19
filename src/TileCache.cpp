#include "TileCache.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QNetworkReply>

TileCache::TileCache(const QString &layerName,
                      const QString &urlTemplate,
                      const QString &userAgent,
                      QObject *parent)
    : QObject(parent)
    , m_layerName(layerName)
    , m_urlTemplate(urlTemplate)
    , m_userAgent(userAgent)
{
    // Кеш на диске: <AppDataLocation>/maps/<layer>/z/x/y.png
    m_cacheRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/maps/" + m_layerName;
}

quint64 TileCache::key(int z, int x, int y)
{
    // 14 бит z, 25 бит x, 25 бит y — с запасом до z=24
    return (quint64(quint32(z)) << 50)
         | (quint64(quint32(x)) << 25)
         |  quint64(quint32(y));
}

QString TileCache::diskPath(int z, int x, int y) const
{
    return QString("%1/%2/%3/%4.png").arg(m_cacheRoot).arg(z).arg(x).arg(y);
}

bool TileCache::tile(int z, int x, int y, QImage &outImage)
{
    const quint64 k = key(z, x, y);

    auto it = m_memCache.constFind(k);
    if (it != m_memCache.constEnd()) {
        outImage = it.value();
        return true;
    }

    const QString path = diskPath(z, x, y);
    if (QFile::exists(path)) {
        QImage img(path);
        if (!img.isNull()) {
            m_memCache.insert(k, img);
            outImage = img;
            return true;
        }
    }

    loadFromDiskOrNetwork(z, x, y);
    return false;
}

void TileCache::loadFromDiskOrNetwork(int z, int x, int y)
{
    const quint64 k = key(z, x, y);
    if (m_pending.contains(k))
        return;
    m_pending.insert(k);

    const QString url = m_urlTemplate.arg(z).arg(x).arg(y);

    const QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, z, x, y, k]() {
        reply->deleteLater();
        m_pending.remove(k);

        if (reply->error() != QNetworkReply::NoError)
            return; // тихо пропускаем; тайл просто не отрисуется

        const QByteArray raw = reply->readAll();
        QImage img;
        if (!img.loadFromData(raw))
            return;

        const QString path = diskPath(z, x, y);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(raw); // пишем исходные байты как есть (без перекодирования)

        m_memCache.insert(k, img);
        emit tileReady(z, x, y);
    });
}
