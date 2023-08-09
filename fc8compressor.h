#ifndef FC8COMPRESSOR_H
#define FC8COMPRESSOR_H

#include <QObject>

class FC8Compressor : public QObject
{
    Q_OBJECT
public:
    explicit FC8Compressor(QByteArray const &data, QObject *parent = NULL);

public slots:
    void doCompression();
    static bool hashMatchesFile(QByteArray const &hash, QByteArray const &file);

signals:
    void compressionFinished(QByteArray hashOfOriginal, QByteArray compressedData);

private:
    QByteArray _data;
};

#endif // FC8COMPRESSOR_H
