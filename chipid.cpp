#include "chipid.h"
#include <QFile>
#include <QRegExp>
#include <QStringList>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#define MySkipEmptyParts Qt::SkipEmptyParts
#else
#define MySkipEmptyParts QString::SkipEmptyParts
#endif

ChipID::ChipID(QString filePath, QObject *parent) : QObject(parent)
{
    QFile f(filePath);
    if (f.open(QFile::ReadOnly))
    {
        loadChips(f);
        f.close();
    }

    dummyChipInfo.manufacturer = "Unknown";
    dummyChipInfo.manufacturerID = 0;
    dummyChipInfo.product = "Unknown";
    dummyChipInfo.productID = 0;
    dummyChipInfo.width = 0;
    dummyChipInfo.capacity = 0;
    dummyChipInfo.unlockShifted = false;
}

bool ChipID::findChips(QList<uint8_t> manufacturersStraight, QList<uint8_t> devicesStraight, QList<uint8_t> manufacturersShifted, QList<uint8_t> devicesShifted, QList<ChipInfo> &info)
{
    // Make sure we have a sane amount of info
    if (manufacturersStraight.count() != 4 || devicesStraight.count() != 4 ||
        manufacturersShifted.count() != 4 || devicesShifted.count() != 4)
    {
        return false;
    }

    QList<QPair<QList<uint8_t>, QList<uint8_t> > > devicesAndManufacturers;
    devicesAndManufacturers << qMakePair(manufacturersStraight, devicesStraight);
    devicesAndManufacturers << qMakePair(manufacturersShifted, devicesShifted);

    // Try both shift types to see if we can identify the chip type
    for (int shiftType = 0; shiftType < 2; shiftType++)
    {
        const bool shifted = shiftType != 0;
        QList<uint8_t> const &manufacturers = devicesAndManufacturers[shiftType].first;
        QList<uint8_t> const &devices = devicesAndManufacturers[shiftType].second;

        ChipInfo const *chipInfo16Bit[2];
        ChipInfo const *chipInfo8Bit[4];
        uint16_t manufacturers16Bit[2];
        uint16_t devices16Bit[2];

        // Look to see if it's a 2-chip SIMM first.
        // Combine the ID values from adjacent chips (assuming 4 chips) into 16-bit wide values
        for (int i = 0; i < 2; i++)
        {
            manufacturers16Bit[i] = manufacturers[2*i] | manufacturers[2*i + 1] << 8;
            devices16Bit[i] = devices[2*i] | devices[2*i + 1] << 8;
            chipInfo16Bit[i] = NULL;
        }

        foreach (ChipInfo const &ci, allChips)
        {
            // Only look for 16-bit chips that match the shift type we're currently looking at
            if (ci.width != 16 || ci.unlockShifted != shifted) { continue; }

            for (int i = 0; i < 2; i++)
            {
                // If we already found a match for this chip, skip it
                if (chipInfo16Bit[i]) { continue; }

                if (ci.manufacturerID == manufacturers16Bit[i] &&
                    ci.productID == devices16Bit[i])
                {
                    chipInfo16Bit[i] = &ci;
                }
            }
        }

        // Now let's try a 4-chip SIMM
        for (int i = 0; i < 4; i++)
        {
            chipInfo8Bit[i] = NULL;
        }

        foreach (ChipInfo const &ci, allChips)
        {
            if (ci.width != 8 || ci.unlockShifted != shifted) { continue; }

            for (int i = 0; i < 4; i++)
            {
                // If we already found a match for this chip, skip it
                if (chipInfo8Bit[i]) { continue; }

                if (ci.manufacturerID == manufacturers[i] &&
                    ci.productID == devices[i])
                {
                    chipInfo8Bit[i] = &ci;
                }
            }
        }

        // How many of each type were a match?
        // If we found any 16-bit matches, assume it's a 2-chip SIMM.
        int matches8Bit = 0;
        int matches16Bit = 0;
        for (int i = 0; i < 2; i++)
        {
            if (chipInfo16Bit[i]) { matches16Bit++; }
        }
        for (int i = 0; i < 4; i++)
        {
            if (chipInfo8Bit[i]) { matches8Bit++; }
        }

        if (matches16Bit > 0)
        {
            // It's a 2-chip SIMM.
            for (int i = 0; i < 2; i++)
            {
                if (chipInfo16Bit[i])
                {
                    info << *chipInfo16Bit[i];
                }
                else
                {
                    ChipInfo dummy = dummyChipInfo;
                    dummy.manufacturerID = manufacturers16Bit[i];
                    dummy.productID = devices16Bit[i];
                    dummy.unlockShifted = shifted;
                    info << dummy;
                }
            }

            return true;
        }
        else if (matches8Bit > 0)
        {
            // It's a 4-chip SIMM.
            for (int i = 0; i < 4; i++)
            {
                if (chipInfo8Bit[i])
                {
                    info << *chipInfo8Bit[i];
                }
                else
                {
                    ChipInfo dummy = dummyChipInfo;
                    dummy.manufacturerID = manufacturers[i];
                    dummy.productID = devices[i];
                    dummy.unlockShifted = shifted;
                    info << dummy;
                }
            }

            return true;
        }
    }

    // If we fall through to here, we didn't find any matches in any method of searching
    return false;
}

void ChipID::loadChips(QIODevice &file)
{
    QRegExp whitespace("\\s+");
    while (!file.atEnd())
    {
        QByteArray line = file.readLine().trimmed();
        if (line.startsWith(";") || line.isEmpty())
        {
            continue;
        }

        QString lineString = QString::fromUtf8(line);
        QStringList components = lineString.split(whitespace, MySkipEmptyParts);
        if (components.count() != 8)
        {
            continue;
        }

        ChipInfo info;
        info.manufacturer = components[0];
        info.product = components[1];
        info.width = components[2].toUInt();
        info.capacity = components[3].toUInt() * 1024;
        QStringList sectorGroups = components[4].split(",");
        foreach (QString const &sectorGroup, sectorGroups)
        {
            QStringList sectorGroupComponents = sectorGroup.split("*");
            QPair<uint16_t, uint32_t> numAndSize;
            if (sectorGroupComponents.count() == 1)
            {
                numAndSize.first = 1;
                numAndSize.second = decodeSectorSize(sectorGroup);
            }
            else if (sectorGroupComponents.count() == 2)
            {
                numAndSize.first = sectorGroupComponents[0].toUInt();
                numAndSize.second = decodeSectorSize(sectorGroupComponents[1]);
            }
            else
            {
                continue;
            }

            if (numAndSize.first != 0 && numAndSize.second != 0)
            {
                info.sectors << numAndSize;
            }
        }

        // Sanity-check the sector list against the total capacity
        uint32_t sectorTotal = 0;
        QPair<uint16_t, uint32_t> numAndSize;
        foreach (numAndSize, info.sectors)
        {
            sectorTotal += numAndSize.first * numAndSize.second;
        }

        // Account for the fact that in 16-bit mode the sector sizes are in words
        if (info.width == 16)
        {
            sectorTotal *= 2;
        }

        if (sectorTotal != info.capacity)
        {
            qWarning("Chip \"%s %s\" has mismatched sector sizes", qPrintable(info.manufacturer), qPrintable(info.product));
            continue;
        }

        info.manufacturerID = components[5].toUInt(NULL, 16);
        info.productID = components[6].toUInt(NULL, 16);
        info.unlockShifted = components[7].toUpper() == "YES";
        allChips.append(info);
    }
}

uint32_t ChipID::decodeSectorSize(QString sizeString)
{
    uint32_t multiplier = 1;
    if (sizeString.endsWith("K"))
    {
        multiplier = 1024;
        sizeString.chop(1);
    }
    else if (sizeString.endsWith("M"))
    {
        multiplier = 1048576;
        sizeString.chop(1);
    }

    bool ok;
    uint32_t number = sizeString.toUInt(&ok);
    if (ok)
    {
        return number * multiplier;
    }

    return 0;
}
