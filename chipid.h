#ifndef CHIPID_H
#define CHIPID_H

#include <QObject>
#include <QIODevice>
#include <QPair>
#include <stdint.h>

class ChipID : public QObject
{
    Q_OBJECT

public:
    struct ChipInfo
    {
        QString manufacturer;
        uint16_t manufacturerID;
        QString product;
        uint16_t productID;
        uint8_t width;
        uint32_t capacity;
        QList<QPair<uint16_t, uint32_t> > sectors;
        bool unlockShifted;
    };

    explicit ChipID(QString filePath, QObject *parent = NULL);

    bool findChips(QList<uint8_t> manufacturersStraight, QList<uint8_t> devicesStraight, QList<uint8_t> manufacturersShifted, QList<uint8_t> devicesShifted, QList<ChipInfo> &info);

private:
    void loadChips(QIODevice &file);
    static uint32_t decodeSectorSize(QString sizeString);

    ChipInfo dummyChipInfo;
    QList<ChipInfo> allChips;
};

#endif // CHIPID_H
