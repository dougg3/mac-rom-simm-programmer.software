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

FC8Compressor::FC8Compressor(const QByteArray &data, int blockSize, QObject *parent) :
    QObject(parent),
    _data(data),
    _blockSize(blockSize)
{

}

void FC8Compressor::doCompression()
{
    QByteArray compressedData(2 * _data.length(), static_cast<char>(0));
    if (_blockSize == 0)
    {
        uint32_t len = fc8::Encode(reinterpret_cast<const uint8_t *>(_data.constData()), _data.length(),
                reinterpret_cast<uint8_t *>(compressedData.data()), compressedData.length());
        // the encode routine returns the compressed length, or 0 if there's an error
        compressedData.truncate(len);
    }
    else
    {
        int numBlocks = (_data.length() - 1) / _blockSize + 1;
        // If the input data isn't a multiple of the block size, make sure we've
        // reserved enough room in the compression buffer
        if (numBlocks * _blockSize > _data.length())
        {
            const int extraBytes = numBlocks * _blockSize - _data.length();
            compressedData.append(QByteArray(2 * extraBytes, static_cast<char>(0)));
        }

        // Fill out the header
        compressedData.replace(0, 4, "FC8b", 4);
        compressedData[FC8_DECODED_SIZE_OFFSET + 0] = (_data.length() >> 24) & 0xFF;
        compressedData[FC8_DECODED_SIZE_OFFSET + 1] = (_data.length() >> 16) & 0xFF;
        compressedData[FC8_DECODED_SIZE_OFFSET + 2] = (_data.length() >> 8) & 0xFF;
        compressedData[FC8_DECODED_SIZE_OFFSET + 3] = (_data.length() >> 0) & 0xFF;
        compressedData[FC8_BLOCK_SIZE_OFFSET + 0] = (_blockSize >> 24) & 0xFF;
        compressedData[FC8_BLOCK_SIZE_OFFSET + 1] = (_blockSize >> 16) & 0xFF;
        compressedData[FC8_BLOCK_SIZE_OFFSET + 2] = (_blockSize >> 8) & 0xFF;
        compressedData[FC8_BLOCK_SIZE_OFFSET + 3] = (_blockSize >> 0) & 0xFF;

        int blockpos = FC8_BLOCK_HEADER_SIZE;
        int pos = FC8_BLOCK_HEADER_SIZE + (4 * numBlocks);
        for (int i = 0; i < numBlocks; i++)
        {
            // Grab another block to write out. Pad it with zeros to the block size if
            // it's the last block and the input data wasn't a multiple of the block size.
            int chunkLen = qMin(_blockSize, _data.length() - (i * _blockSize));
            QByteArray block = QByteArray::fromRawData(_data.constData() + i * _blockSize, chunkLen);
            if (chunkLen < _blockSize)
            {
                block.append(QByteArray(_blockSize - chunkLen, static_cast<char>(0)));
            }

            // Compress this block
            uint32_t len = fc8::Encode(reinterpret_cast<const uint8_t *>(block.constData()), _blockSize,
                    reinterpret_cast<uint8_t *>(compressedData.data() + pos), compressedData.length() - pos);
            if (len == 0)
            {
                // Error occurred during encoding. Signal with an empty QByteArray to signal an error
                compressedData.clear();
                break;
            }

            // Save the start location of this block in the block table
            compressedData[blockpos + 0] = (pos >> 24) & 0xFF;
            compressedData[blockpos + 1] = (pos >> 16) & 0xFF;
            compressedData[blockpos + 2] = (pos >> 8) & 0xFF;
            compressedData[blockpos + 3] = (pos >> 0) & 0xFF;

            // Move forward
            blockpos += 4;
            pos += len;
        }

        // All done; now truncate the compressed array to where we left off
        compressedData.truncate(pos);
    }

    // Calculate a signature of the original file so we can associate the compressed version
    // with the original.
    QByteArray hashOfOriginal = QCryptographicHash::hash(_data, hashAlgorithm());
    emit compressionFinished(hashOfOriginal, compressedData);
}

bool FC8Compressor::hashMatchesFile(const QByteArray &hash, const QByteArray &file)
{
    return QCryptographicHash::hash(file, hashAlgorithm()) == hash;
}
