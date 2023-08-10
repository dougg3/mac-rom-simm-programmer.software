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

signals:
    void compressionFinished(QByteArray compressedData);

private:
    QByteArray _data;
};

#endif // FC8COMPRESSOR_H
