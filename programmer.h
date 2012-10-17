/*
 * Copyright (C) 2011-2012 Doug Brown
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef PROGRAMMER_H
#define PROGRAMMER_H

#include <QObject>
#include <QFile>
#include <QIODevice>
#include <qextserialport.h>
#include <qextserialenumerator.h>
#include <stdint.h>
#include <QBuffer>

typedef enum StartStatus
{
    ProgrammerInitializing,
    ProgrammerInitialized
} StartStatus;

typedef enum ReadStatus
{
    ReadStarting,
    ReadComplete,
    ReadError,
    ReadCancelled,
    ReadTimedOut
} ReadStatus;

typedef enum WriteStatus
{
    WriteErasing,
    WriteCompleteNoVerify,
    WriteError,
    WriteCancelled,
    WriteEraseComplete,
    WriteEraseFailed,
    WriteTimedOut,
    WriteFileTooBig,
    WriteNeedsFirmwareUpdateBiggerSIMM,
    WriteNeedsFirmwareUpdateVerifyWhileWrite,
    WriteVerifying,
    WriteVerificationFailure,
    WriteVerifyStarting,
    WriteVerifyError,
    WriteVerifyCancelled,
    WriteVerifyTimedOut,
    WriteCompleteVerifyOK,
    WriteEraseBlockWrongSize,
    WriteNeedsFirmwareUpdateErasePortion
} WriteStatus;

typedef enum ElectricalTestStatus
{
    ElectricalTestStarted,
    ElectricalTestPassed,
    ElectricalTestFailed,
    ElectricalTestTimedOut,
    ElectricalTestCouldntStart
} ElectricalTestStatus;

typedef enum IdentificationStatus
{
    IdentificationStarting,
    IdentificationComplete,
    IdentificationError,
    IdentificationTimedOut,
    IdentificationNeedsFirmwareUpdate
} IdentificationStatus;

typedef enum FirmwareFlashStatus
{
    FirmwareFlashStarting,
    FirmwareFlashComplete,
    FirmwareFlashError,
    FirmwareFlashCancelled,
    FirmwareFlashTimedOut
} FirmwareFlashStatus;

// Various choices for verification
typedef enum VerificationOption
{
    NoVerification,
    VerifyWhileWriting,
    VerifyAfterWrite
} VerificationOption;

// Electrical test indexes
#define GROUND_FAIL_INDEX					0xFF

#define FIRST_ADDRESS_LINE_FAIL_INDEX		0
#define LAST_ADDRESS_LINE_FAIL_INDEX		(FIRST_ADDRESS_LINE_FAIL_INDEX + 20)
#define FIRST_DATA_LINE_FAIL_INDEX			(LAST_ADDRESS_LINE_FAIL_INDEX + 1)
#define LAST_DATA_LINE_FAIL_INDEX			(FIRST_DATA_LINE_FAIL_INDEX + 31)
#define CS_FAIL_INDEX						(LAST_DATA_LINE_FAIL_INDEX + 1)
#define OE_FAIL_INDEX						(CS_FAIL_INDEX + 1)
#define WE_FAIL_INDEX						(OE_FAIL_INDEX + 1)

class Programmer : public QObject
{
    Q_OBJECT
public:
    explicit Programmer(QObject *parent = 0);
    virtual ~Programmer();
    void readSIMM(QIODevice *device, uint32_t len = 0);
    void writeToSIMM(QIODevice *device);
    void writeToSIMM(QIODevice *device, uint32_t startOffset, uint32_t length);
    void runElectricalTest();
    QString electricalTestPinName(uint8_t index);
    void identifySIMMChips();
    void getChipIdentity(int chipIndex, uint8_t *manufacturer, uint8_t *device);
    void flashFirmware(QString filename);
    void startCheckingPorts();
    void setSIMMCapacity(uint32_t bytes);
    uint32_t SIMMCapacity() const;
    void setVerifyMode(VerificationOption mode);
    VerificationOption verifyMode() const;
    uint8_t verifyBadChipMask() const { return _verifyBadChipMask; }
signals:
    void startStatusChanged(StartStatus status);

    void readStatusChanged(ReadStatus status);
    void readTotalLengthChanged(uint32_t total);
    void readCompletionLengthChanged(uint32_t total);

    void writeStatusChanged(WriteStatus status);
    void writeTotalLengthChanged(uint32_t total);
    void writeCompletionLengthChanged(uint32_t len);
    void writeVerifyTotalLengthChanged(uint32_t total);
    void writeVerifyCompletionLengthChanged(uint32_t total);

    void electricalTestStatusChanged(ElectricalTestStatus status);
    void electricalTestFailLocation(uint8_t loc1, uint8_t loc2);

    void identificationStatusChanged(IdentificationStatus status);

    void firmwareFlashStatusChanged(FirmwareFlashStatus status);
    void firmwareFlashTotalLengthChanged(uint32_t total);
    void firmwareFlashCompletionLengthChanged(uint32_t total);

    void programmerBoardConnected();
    void programmerBoardDisconnected();
    void programmerBoardDisconnectedDuringOperation();
public slots:

private:
    //QFile *readFile;
    //QFile *writeFile;
    QIODevice *readDevice;
    QIODevice *writeDevice;
    QFile *firmwareFile;

    QextSerialPort *serialPort;
    void sendByte(uint8_t b);
    void sendWord(uint32_t w);
    uint8_t readByte();
    void handleChar(uint8_t c);
    uint32_t _simmCapacity;

    uint32_t writeLenRemaining;
    uint32_t lenWritten;
    uint32_t electricalTestErrorCounter;
    uint8_t electricalTestFirstErrorLoc;

    uint32_t readChunkLenRemaining;
    uint32_t lenRead;
    uint32_t trueLenToRead;
    uint32_t lenRemaining;

    int identificationCounter;
    uint8_t chipManufacturerIDs[4];
    uint8_t chipDeviceIDs[4];

    uint32_t firmwareLenRemaining;
    uint32_t firmwareLenWritten;

    VerificationOption _verifyMode;
    uint8_t _verifyBadChipMask;
    bool isReadVerifying;
    QBuffer *verifyBuffer;
    QByteArray *verifyArray;

    uint32_t writeOffset;
    uint32_t writeLength;

    void openPort();
    void closePort();

    void internalReadSIMM(QIODevice *device, uint32_t len);
    void startProgrammerCommand(uint8_t commandByte, uint32_t newState);
    void startBootloaderCommand(uint8_t commandByte, uint32_t newState);
    void doVerifyAfterWriteCompare();

private slots:
    void dataReady();

    void portDiscovered(const QextPortInfo &info);
    void portDiscovered_internal();
    void portRemoved(const QextPortInfo &info);
};

#endif // PROGRAMMER_H
