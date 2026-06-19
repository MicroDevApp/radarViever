#pragma once

#include <QObject>
#include <QImage>
#include <QNetworkAccessManager>
#include <QHash>
#include <QSet>
#include <QString>

// Универсальный кеш тайлов одного слоя (например "osm" или "dem").
// Тайл ищется в порядке: память -> диск (~/.../maps/<layer>/z/x/y.png) -> сеть.
// tile() никогда не блокирует на сети: если тайла нет, она запускает
// фоновую загрузку и возвращает false; когда тайл будет готов, придёт
// сигнал tileReady(z,x,y) и можно повторно вызвать tile().
class TileCache : public QObject
{
    Q_OBJECT
public:
    // urlTemplate: "%1"=z "%2"=x "%3"=y, например
    // "https://tile.openstreetmap.org/%1/%2/%3.png"
    TileCache(const QString &layerName,
              const QString &urlTemplate,
              const QString &userAgent,
              QObject *parent = nullptr);

    // Возвращает true и заполняет outImage, если тайл уже доступен
    // (память или диск). Если false — тайл поставлен в очередь загрузки.
    bool tile(int z, int x, int y, QImage &outImage);

    static quint64 key(int z, int x, int y);

signals:
    void tileReady(int z, int x, int y);

private:
    void startNetworkRequest(int z, int x, int y);
    QString diskPath(int z, int x, int y) const;
    void loadFromDiskOrNetwork(int z, int x, int y);

    QString m_layerName;
    QString m_urlTemplate;
    QString m_userAgent;
    QString m_cacheRoot;

    QNetworkAccessManager m_nam;
    QHash<quint64, QImage> m_memCache;
    QSet<quint64> m_pending;
};
