#include "fc8compressor.h"
#include <stdint.h>
namespace fc8 {
extern "C" {
#include "3rdparty/fc8-compression/fc8.h"
}
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
    emit compressionFinished(compressedData);
}
