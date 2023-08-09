#include "fc8compressor.h"
#include <QCryptographicHash>
#include <stdint.h>
namespace fc8 {
extern "C" {
#include "3rdparty/fc8-compression/fc8.h"
}
}

static QCryptographicHash::Algorithm hashAlgorithm()
{
    // Preserve Qt 4 compatibility, just in case...
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
    return QCryptographicHash::Sha256;
#else
    return QCryptographicHash::Sha1;
#endif
}

FC8Compressor::FC8Compressor(const QByteArray &data, QObject *parent) :
    QObject(parent),
    _data(data)
{

}

void FC8Compressor::doCompression()
{
    QByteArray compressedData(FC8_HEADER_SIZE + _data.length(), static_cast<char>(0));
    uint32_t len = fc8::Encode(reinterpret_cast<const uint8_t *>(_data.constData()), _data.length(),
                reinterpret_cast<uint8_t *>(compressedData.data()), compressedData.length());
    // the encode routine returns the compressed length, or 0 if there's an error
    compressedData.truncate(len);

    // Calculate a signature of the original file so we can associate the compressed version
    // with the original.
    QByteArray hashOfOriginal = QCryptographicHash::hash(_data, hashAlgorithm());
    emit compressionFinished(hashOfOriginal, compressedData);
}

bool FC8Compressor::hashMatchesFile(const QByteArray &hash, const QByteArray &file)
{
    return QCryptographicHash::hash(file, hashAlgorithm()) == hash;
}
