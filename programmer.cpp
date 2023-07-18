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

#include "programmer.h"
#include <QDebug>
#include <QWaitCondition>
#include <QMutex>

typedef enum ProgrammerCommandState
{
    WaitingForNextCommand = 0,

    WriteSIMMWaitingSetSizeReply,
    WriteSIMMWaitingSetVerifyModeReply,
    WriteSIMMWaitingSetChipMaskReply,
    WriteSIMMWaitingSetChipMaskValueReply,
    WriteSIMMWaitingEraseReply,
    WriteSIMMWaitingWriteReply,
    WriteSIMMWaitingFinishReply,
    WriteSIMMWaitingWriteMoreReply,

    ElectricalTestWaitingStartReply,
    ElectricalTestWaitingNextStatus,
    ElectricalTestWaitingFirstFail,
    ElectricalTestWaitingSecondFail,

    ReadSIMMWaitingStartReply,
    ReadSIMMWaitingStartOffsetReply,
    ReadSIMMWaitingLengthReply,
    ReadSIMMWaitingData,
    ReadSIMMWaitingStatusReply,

    BootloaderStateAwaitingOKReply,
    BootloaderStateAwaitingReply,
    BootloaderStateAwaitingOKReplyToBootloader,
    BootloaderStateAwaitingReplyToBootloader,
    BootloaderStateAwaitingUnplug,
    BootloaderStateAwaitingPlug,
    BootloaderStateAwaitingUnplugToBootloader,
    BootloaderStateAwaitingPlugToBootloader,

    IdentificationWaitingSetSizeReply,
    IdentificationAwaitingOKReply,
    IdentificationWaitingData,
    IdentificationAwaitingDoneReply,

    BootloaderEraseProgramAwaitingStartOKReply,
    BootloaderEraseProgramWaitingFinishReply,
    BootloaderEraseProgramWaitingWriteMoreReply,
    BootloaderEraseProgramWaitingWriteReply,

    WritePortionWaitingSetSizeReply,
    WritePortionWaitingSetVerifyModeReply,
    WritePortionWaitingSetChipMaskReply,
    WritePortionWaitingSetChipMaskValueReply,
    WritePortionWaitingEraseReply,
    WritePortionWaitingEraseConfirmation,
    WritePortionWaitingEraseResult,
    WritePortionWaitingWriteAtReply
} ProgrammerCommandState;

typedef enum ProgrammerBoardFoundState
{
    ProgrammerBoardNotFound,
    ProgrammerBoardFound
} ProgrammerBoardFoundState;

typedef enum ProgrammerCommand
{
    EnterWaitingMode = 0,
    DoElectricalTest,
    IdentifyChips,
    ReadByte,
    ReadChips,
    EraseChips,
    WriteChips,
    GetBootloaderState,
    EnterBootloader,
    EnterProgrammer,
    BootloaderEraseAndWriteProgram,
    SetSIMMLayout_AddressStraight,
    SetSIMMLayout_AddressShifted,
    SetVerifyWhileWriting,
    SetNoVerifyWhileWriting,
    ErasePortion,
    WriteChipsAt,
    ReadChipsAt,
    SetChipsMask
} ProgrammerCommand;

typedef enum ProgrammerReply
{
    CommandReplyOK,
    CommandReplyError,
    CommandReplyInvalid
} ProgrammerReply;

typedef enum ComputerReadReply
{
    ComputerReadOK,
    ComputerReadCancel
} ComputerReadReply;

typedef enum ProgrammerReadReply
{
    ProgrammerReadOK,
    ProgrammerReadError,
    ProgrammerReadMoreData,
    ProgrammerReadFinished,
    ProgrammerReadConfirmCancel
} ProgrammerReadReply;

typedef enum ComputerWriteReply
{
    ComputerWriteMore,
    ComputerWriteFinish,
    ComputerWriteCancel
} ComputerWriteReply;

typedef enum ProgrammerWriteReply
{
    ProgrammerWriteOK,
    ProgrammerWriteError,
    ProgrammerWriteConfirmCancel,
    ProgrammerWriteVerificationError = 0x80 /* high bit */
} ProgrammerWriteReply;

typedef enum ProgrammerIdentifyReply
{
    ProgrammerIdentifyDone
} ProgrammerIdentifyReply;

typedef enum ProgrammerElectricalTestReply
{
    ProgrammerElectricalTestFail,
    ProgrammerElectricalTestDone
} ProgrammerElectricalTestReply;

typedef enum BootloaderStateReply
{
    BootloaderStateInBootloader,
    BootloaderStateInProgrammer
} BootloaderStateReply;

typedef enum ProgrammerBootloaderEraseWriteReply
{
    BootloaderWriteOK,
    BootloaderWriteError,
    BootloaderWriteConfirmCancel
} ProgrammerBootloaderEraseWriteReply;

typedef enum ComputerBootloaderEraseWriteRequest
{
    ComputerBootloaderWriteMore = 0,
    ComputerBootloaderFinish,
    ComputerBootloaderCancel
} ComputerBootloaderEraseWriteRequest;

typedef enum ProgrammerErasePortionOfChipReply
{
    ProgrammerErasePortionOK = 0,
    ProgrammerErasePortionError,
    ProgrammerErasePortionFinished
} ProgrammerErasePortionOfChipReply;

#define PROGRAMMER_USB_VENDOR_ID            0x16D0
#define PROGRAMMER_USB_DEVICE_ID            0x06AA


#define WRITE_CHUNK_SIZE    1024
#define READ_CHUNK_SIZE     1024
#define FIRMWARE_CHUNK_SIZE 1024

#define BLOCK_ERASE_SIZE    (256*1024UL)

static ProgrammerCommandState curState = WaitingForNextCommand;

// After identifying that we're in the main program, what will be the command
// we will send and the state we will be waiting in?
static ProgrammerCommandState nextState = WaitingForNextCommand;
static uint8_t nextSendByte = 0;

static ProgrammerBoardFoundState foundState = ProgrammerBoardNotFound;
static QString programmerBoardPortName;

Programmer::Programmer(QObject *parent) :
    QObject(parent)
{
    _verifyMode = VerifyAfterWrite;
    _verifyBadChipMask = 0;
    verifyArray = new QByteArray();
    verifyBuffer = new QBuffer(verifyArray);
    verifyBuffer->open(QBuffer::ReadWrite);
    serialPort = new QextSerialPort(QextSerialPort::EventDriven);
    connect(serialPort, SIGNAL(readyRead()), SLOT(dataReady()));
}

Programmer::~Programmer()
{
    closePort();
    delete serialPort;
    verifyBuffer->close();
    delete verifyBuffer;
    delete verifyArray;
}

void Programmer::readSIMM(QIODevice *device, uint32_t len)
{
    // We're not verifying in this case
    isReadVerifying = false;
    internalReadSIMM(device, len);
}

void Programmer::internalReadSIMM(QIODevice *device, uint32_t len, uint32_t offset)
{
    readDevice = device;
    lenRead = 0;
    readOffset = offset;

    // Len == 0 means read the entire SIMM
    if (len == 0)
    {
        lenRemaining = _simmCapacity;
        trueLenToRead = _simmCapacity;
    }
    else if (len % READ_CHUNK_SIZE)
    {
        // We have to read a full chunk of data, so we read a little bit
        // past the actual length requested but only return the amount
        // requested.
        uint32_t lastExtraChunk = (len % READ_CHUNK_SIZE);
        lenRemaining = len - lastExtraChunk + READ_CHUNK_SIZE;
        trueLenToRead = len;
    }
    else // already a multiple of READ_CHUNK_SIZE, no correction needed
    {
        lenRemaining = len;
        trueLenToRead = len;
    }

    if (offset > 0)
    {
        startProgrammerCommand(ReadChipsAt, ReadSIMMWaitingStartOffsetReply);
    }
    else
    {
        startProgrammerCommand(ReadChips, ReadSIMMWaitingStartReply);
    }
}

void Programmer::writeToSIMM(QIODevice *device, uint8_t chipsMask)
{
    writeDevice = device;
    writeChipMask = chipsMask;
    if (writeDevice->size() > SIMMCapacity())
    {
        curState = WaitingForNextCommand;
        emit writeStatusChanged(WriteFileTooBig);
        return;
    }
    else
    {
        lenWritten = 0;
        writeLenRemaining = writeDevice->size();
        writeOffset = 0;

        // Based on the SIMM type, tell the programmer board.
        uint8_t setLayoutCommand;
	if(SIMMChip()==SIMM_TSOP_x8)
        {
            setLayoutCommand = SetSIMMLayout_AddressShifted;
        }
        else
        {
            setLayoutCommand = SetSIMMLayout_AddressStraight;
        }
        startProgrammerCommand(setLayoutCommand, WriteSIMMWaitingSetSizeReply);
    }
}

void Programmer::writeToSIMM(QIODevice *device, uint32_t startOffset, uint32_t length, uint8_t chipsMask)
{
    writeDevice = device;
    writeChipMask = chipsMask;
    if ((writeDevice->size() > SIMMCapacity()) ||
         (startOffset + length > SIMMCapacity()))
    {
        curState = WaitingForNextCommand;
        emit writeStatusChanged(WriteFileTooBig);
        return;
    }
    else if ((startOffset % BLOCK_ERASE_SIZE) || (length % BLOCK_ERASE_SIZE))
    {
        curState = WaitingForNextCommand;
        emit writeStatusChanged(WriteEraseBlockWrongSize);
        return;
    }
    else
    {
        lenWritten = 0;
        writeLenRemaining = writeDevice->size() - startOffset;
        if (writeLenRemaining > length)
        {
            writeLenRemaining = length;
        }
        device->seek(startOffset);
        writeOffset = startOffset;
        writeLength = length;

        // Based on the SIMM type, tell the programmer board.
        uint8_t setLayoutCommand;
	if (SIMMChip() == SIMM_TSOP_x8)
        {
            setLayoutCommand = SetSIMMLayout_AddressShifted;
        }
        else
        {
            setLayoutCommand = SetSIMMLayout_AddressStraight;
        }
        startProgrammerCommand(setLayoutCommand, WritePortionWaitingSetSizeReply);
    }
}

void Programmer::sendByte(uint8_t b)
{
    serialPort->write((const char *)&b, 1);
}

void Programmer::sendWord(uint32_t w)
{
    sendByte((w >> 0)  & 0xFF);
    sendByte((w >> 8)  & 0xFF);
    sendByte((w >> 16) & 0xFF);
    sendByte((w >> 24) & 0xFF);
}

uint8_t Programmer::readByte()
{
    uint8_t returnVal;
    serialPort->read((char *)&returnVal, 1);
    // TODO: Error checking if read fails?
    return returnVal;
}

void Programmer::dataReady()
{
    while (!serialPort->atEnd())
    {
        handleChar(readByte());
    }
}

void Programmer::handleChar(uint8_t c)
{
    switch (curState)
    {
    case WaitingForNextCommand:
        // Not expecting anything. Ignore it.
        break;

    // Expecting reply after we told the programmer the size of SIMM to expect
    case WriteSIMMWaitingSetSizeReply:
    case WritePortionWaitingSetSizeReply:
        switch (c)
        {
        case CommandReplyOK:
            // If we got an OK reply, we're good to go. Next, check for the
            // "verify while writing" capability if needed...

            uint8_t verifyCommand;
            if (verifyMode() == VerifyWhileWriting)
            {
                verifyCommand = SetVerifyWhileWriting;
            }
            else
            {
                verifyCommand = SetNoVerifyWhileWriting;
            }

            if (curState == WriteSIMMWaitingSetSizeReply)
            {
                curState = WriteSIMMWaitingSetVerifyModeReply;
            }
            else if (curState == WritePortionWaitingSetSizeReply)
            {
                curState = WritePortionWaitingSetVerifyModeReply;
            }
            sendByte(verifyCommand);
            break;
        case CommandReplyInvalid:
        case CommandReplyError:
            // If we got an error reply, we MAY still be OK unless we were
            // requesting the large SIMM type, in which case the firmware
            // doesn't support the large SIMM type so the user needs to know.
            if (SIMMChip() != SIMM_PLCC_x8)
            {
                // Uh oh -- this is an old firmware that doesn't support a big
                // SIMM. Let the caller know that the programmer board needs a
                // firmware update.
                qDebug() << "Programmer board needs firmware update.";
                curState = WaitingForNextCommand;
                closePort();
                emit writeStatusChanged(WriteNeedsFirmwareUpdateBiggerSIMM);
            }
            else
            {
                // Error reply, but we're writing a small SIMM, so the firmware
                // doesn't need updating -- it just didn't know how to handle
                // the "set size" command. But that's OK -- it only supports
                // the size we requested, so nothing's wrong.

                // So...check for the "verify while writing" capability if needed.
                uint8_t verifyCommand;
                if (verifyMode() == VerifyWhileWriting)
                {
                    verifyCommand = SetVerifyWhileWriting;
                }
                else
                {
                    verifyCommand = SetNoVerifyWhileWriting;
                }

                if (curState == WriteSIMMWaitingSetSizeReply)
                {
                    curState = WriteSIMMWaitingSetVerifyModeReply;
                }
                else if (curState == WritePortionWaitingSetSizeReply)
                {
                    curState = WritePortionWaitingSetVerifyModeReply;
                }
                sendByte(verifyCommand);
            }
            break;
        }

        break;

    // Expecting reply from programmer after we told it to verify during write
    // (or not to verify during write)
    case WriteSIMMWaitingSetVerifyModeReply:
    case WritePortionWaitingSetVerifyModeReply:
        switch (c)
        {
        case CommandReplyOK:
            // If we got an OK reply, we're good. Now try to set the chip mask.
            if (curState == WriteSIMMWaitingSetVerifyModeReply)
            {
                sendByte(SetChipsMask);
                curState = WriteSIMMWaitingSetChipMaskReply;
            }
            else if (curState == WritePortionWaitingSetVerifyModeReply)
            {
                sendByte(SetChipsMask);
                curState = WritePortionWaitingSetChipMaskReply;
            }
            break;
        case CommandReplyInvalid:
        case CommandReplyError:
            // If we got an error reply, we MAY still be OK unless we were
            // asking to verify while writing, in which case the firmware
            // doesn't support verify during write so the user needs to know.
            if (verifyMode() == VerifyWhileWriting)
            {
                // Uh oh -- this is an old firmware that doesn't support verify
                // while write. Let the caller know that the programmer board
                // needs a firmware update.
                qDebug() << "Programmer board needs firmware update.";
                curState = WaitingForNextCommand;
                closePort();
                emit writeStatusChanged(WriteNeedsFirmwareUpdateVerifyWhileWrite);
            }
            else
            {
                // Error reply, but we're not trying to verify while writing, so
                // the firmware doesn't need updating -- it just didn't know how to handle
                // the "set verify mode" command. But that's OK -- we don't need
                // that command if we're not verifying while writing.

                // So move onto the next thing to try.
                if (curState == WriteSIMMWaitingSetVerifyModeReply)
                {
                    sendByte(SetChipsMask);
                    curState = WriteSIMMWaitingSetChipMaskReply;
                }
                else if (curState == WritePortionWaitingSetVerifyModeReply)
                {
                    sendByte(SetChipsMask);
                    curState = WritePortionWaitingSetChipMaskReply;
                }
            }
            break;
        }

        break;

    case WriteSIMMWaitingSetChipMaskReply:
    case WritePortionWaitingSetChipMaskReply:
        switch (c)
        {
        case CommandReplyOK:
            // OK, now we can send the chip mask and move onto the next state
            sendByte(writeChipMask);
            if (curState == WriteSIMMWaitingSetChipMaskReply)
            {
                curState = WriteSIMMWaitingSetChipMaskValueReply;
            }
            else if (curState == WritePortionWaitingSetChipMaskReply)
            {
                curState = WritePortionWaitingSetChipMaskValueReply;
            }
            break;
        case CommandReplyInvalid:
        case CommandReplyError:
            // Error reply. If we're trying to set a mask of 0x0F, no error, it
            // just means the firmware's out of date and doesn't support setting
            // custom chip masks. Ignore and move on.
            if (writeChipMask == 0x0F)
            {
                // OK, erase the SIMM and get the ball rolling.
                // Special case: Send out notification we are starting an erase command.
                // I don't have any hooks into the process between now and the erase reply.
                emit writeStatusChanged(WriteErasing);
                if (curState == WriteSIMMWaitingSetChipMaskReply)
                {
                    sendByte(EraseChips);
                    curState = WriteSIMMWaitingEraseReply;
                }
                else if (curState == WritePortionWaitingSetChipMaskReply)
                {
                    sendByte(ErasePortion);
                    curState = WritePortionWaitingEraseReply;
                }
            }
            else
            {
                // Uh oh -- this is an old firmware that doesn't support custom
                // chip masks. Let the caller know that the programmer board
                // needs a firmware update.
                qDebug() << "Programmer board needs firmware update.";
                curState = WaitingForNextCommand;
                closePort();
                emit writeStatusChanged(WriteNeedsFirmwareUpdateIndividualChips);
            }
            break;
        }

        break;

    case WriteSIMMWaitingSetChipMaskValueReply:
    case WritePortionWaitingSetChipMaskValueReply:
        switch (c)
        {
        case CommandReplyOK:
            // OK, erase the SIMM and get the ball rolling.
            // Special case: Send out notification we are starting an erase command.
            // I don't have any hooks into the process between now and the erase reply.
            emit writeStatusChanged(WriteErasing);
            if (curState == WriteSIMMWaitingSetChipMaskValueReply)
            {
                sendByte(EraseChips);
                curState = WriteSIMMWaitingEraseReply;
            }
            else if (curState == WritePortionWaitingSetChipMaskValueReply)
            {
                sendByte(ErasePortion);
                curState = WritePortionWaitingEraseReply;
            }
            break;
        case CommandReplyInvalid:
        case CommandReplyError:
            // Error after trying to set the value.
            qDebug() << "Error reply setting chip mask.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteError);
            break;
        }

        break;

    // Expecting reply from programmer after we told it to erase the chip
    case WriteSIMMWaitingEraseReply:
    {
        switch (c)
        {
        case CommandReplyOK:
            sendByte(WriteChips);
            curState = WriteSIMMWaitingWriteReply;
            qDebug() << "Chips erased. Now asking to start writing...";
            emit writeStatusChanged(WriteEraseComplete);
            emit writeTotalLengthChanged(writeLenRemaining);
            emit writeCompletionLengthChanged(lenWritten);
            break;
        case CommandReplyError:
            qDebug() << "Error erasing chips.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteEraseFailed);
            break;
        }
        break;
    }

    case WritePortionWaitingEraseReply:
    {
        switch (c)
        {
        case CommandReplyOK:
            sendWord(writeOffset);
            sendWord(writeLength);
            qDebug("Sending %u, %u", writeOffset, writeLength);
            curState = WritePortionWaitingEraseConfirmation;
            qDebug() << "Sent erase positions, waiting for reply...";
            break;
        case CommandReplyError:
            // Uh oh -- this is an old firmware that doesn't support verify
            // while write. Let the caller know that the programmer board
            // needs a firmware update.
            qDebug() << "Programmer board needs firmware update.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteNeedsFirmwareUpdateErasePortion);
            break;
        }
        break;
    }

    case WritePortionWaitingEraseConfirmation:
    {
        switch (c)
        {
        case ProgrammerErasePortionOK:
            curState = WritePortionWaitingEraseResult;
            break;
        case ProgrammerErasePortionError:
            // Programmer didn't like the position/length we gave it
            qDebug() << "Programmer didn't like erase pos/length.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteEraseFailed);
            break;
        }

        break;
    }

    case WritePortionWaitingEraseResult:
    {
        switch (c)
        {
        case ProgrammerErasePortionFinished:
            // we're done erasing, now it's time to write the data
            // starting at where we wanted to flash to
            sendByte(WriteChipsAt);
            curState = WritePortionWaitingWriteAtReply;
            qDebug() << "Chips partially erased. Now asking to start writing...";
            emit writeStatusChanged(WriteEraseComplete);
            break;
        case ProgrammerErasePortionError:
            // Programmer failed to erase
            qDebug() << "Programmer had error erasing.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteEraseFailed);
            break;
        }

        break;
    }

    case WritePortionWaitingWriteAtReply:
    {
        switch (c)
        {
        case CommandReplyOK:
            sendWord(writeOffset);
            qDebug() << "Sending" << writeOffset;
            curState = WriteSIMMWaitingWriteReply;
            emit writeTotalLengthChanged(writeLenRemaining);
            emit writeCompletionLengthChanged(lenWritten);
            qDebug() << "Partial write command accepted, sending offset...";
            break;
        case CommandReplyError:
        case CommandReplyInvalid:
        default:
            // Programmer failed to erase
            qDebug() << "Programmer didn't accept 'write at' command.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteError);
            break;
        }

        break;
    }

    // Expecting reply from programmer after we sent a chunk of data to write
    // (or after we first told it we're going to start writing)
    case WriteSIMMWaitingWriteReply:
        // This is a special case in the protocol for efficiency.
        if (c & ProgrammerWriteVerificationError)
        {
            _verifyBadChipMask = c & ~ProgrammerWriteVerificationError;
            qDebug() << "Verification error during write.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteVerificationFailure);
            break;
        }
        else
        {
            switch (c)
            {
            case CommandReplyOK:
                // We're in write SIMM mode. Now ask to start writing
                if (writeLenRemaining > 0)
                {
                    sendByte(ComputerWriteMore);
                    curState = WriteSIMMWaitingWriteMoreReply;
                    qDebug() << "Write more..." << writeLenRemaining << "remaining.";
                }
                else
                {
                    sendByte(ComputerWriteFinish);
                    curState = WriteSIMMWaitingFinishReply;
                    qDebug() << "Finished writing. Sending write finish command...";
                }
                break;
            case CommandReplyError:
                qDebug() << "Error entering write mode.";
                curState = WaitingForNextCommand;
                closePort();
                emit writeStatusChanged(WriteError);
                break;
            }
        }

        break;

    // Expecting reply from programmer after we requested to write another data chunk
    case WriteSIMMWaitingWriteMoreReply:
    {
        qDebug() << "Write more reply:" << c;
        switch (c)
        {
        case ProgrammerWriteOK:
        {
            qDebug() << "Programmer replied OK to send 1024 bytes of data! Sending...";
            // Write the next chunk of data to the SIMM...

            int chunkSize = WRITE_CHUNK_SIZE;
            if (writeLenRemaining < WRITE_CHUNK_SIZE)
            {
                chunkSize = writeLenRemaining;
            }

            // Read the chunk from the file!
            QByteArray thisChunk = writeDevice->read(chunkSize);

            // If it isn't a WRITE_CHUNK_SIZE chunk, pad the rest of it with 0xFFs (unprogrammed bytes)
            // so the total chunk size is WRITE_CHUNK_SIZE, since that's what the programmer board expects.
            for (int x = writeLenRemaining; x < WRITE_CHUNK_SIZE; x++)
            {
                thisChunk.append(0xFF);
            }

            // Write the chunk out (it's asynchronous so will return immediately)
            serialPort->write(thisChunk);

            // OK, now we're waiting to hear back from the programmer on the result
            qDebug() << "Waiting for status reply...";
            curState = WriteSIMMWaitingWriteReply;
            writeLenRemaining -= chunkSize;
            lenWritten += chunkSize;
            emit writeCompletionLengthChanged(lenWritten);
            break;
        }
        case ProgrammerWriteError:
        default:
            qDebug() << "Error writing to chips.";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteError);
            break;
        }
        break;
    }

    // Expecting reply from programmer after we told it we're done writing
    case WriteSIMMWaitingFinishReply:
        switch (c)
        {
        case ProgrammerWriteOK:
            if (verifyMode() == VerifyAfterWrite)
            {
                isReadVerifying = true;

                // Ensure the verify buffer is empty
                verifyArray->clear();
                verifyBuffer->seek(0);
                verifyLength = lenWritten;

                // Start reading from the SIMM now!
                emit writeStatusChanged(WriteVerifying);
                internalReadSIMM(verifyBuffer, writeDevice->size());
            }
            else
            {
                curState = WaitingForNextCommand;
                qDebug() << "Write success at end";
                closePort();

                // Emit the correct signal based on how we finished
                if (verifyMode() == NoVerification)
                {
                    emit writeStatusChanged(WriteCompleteNoVerify);
                }
                else
                {
                    emit writeStatusChanged(WriteCompleteVerifyOK);
                }
            }

            break;
        case ProgrammerWriteError:
        default:
            qDebug() << "Write failure at end";
            curState = WaitingForNextCommand;
            closePort();
            emit writeStatusChanged(WriteError);
            break;
        }

        break;

    // ELECTRICAL TEST STATE HANDLERS

    // Expecting reply from programmer after we told it to run an electrical test
    case ElectricalTestWaitingStartReply:
        switch (c)
        {
        case CommandReplyOK:
            curState = ElectricalTestWaitingNextStatus;
            emit electricalTestStatusChanged(ElectricalTestStarted);
            electricalTestErrorCounter = 0;
            break;
        case CommandReplyError:
        case CommandReplyInvalid:
        default:
            curState = WaitingForNextCommand;
            closePort();
            emit electricalTestStatusChanged(ElectricalTestCouldntStart);
        }
        break;

    // Expecting info from programmer about the electrical test in progress
    // (Either that it's done or that it found a failure)
    case ElectricalTestWaitingNextStatus:
        switch (c)
        {
        case ProgrammerElectricalTestDone:
            curState = WaitingForNextCommand;
            closePort();
            if (electricalTestErrorCounter > 0)
            {
                emit electricalTestStatusChanged(ElectricalTestFailed);
            }
            else
            {
                emit electricalTestStatusChanged(ElectricalTestPassed);
            }
            break;
        case ProgrammerElectricalTestFail:
            electricalTestErrorCounter++;
            curState = ElectricalTestWaitingFirstFail;
            break;
        }
        break;
    // Expecting electrical test fail location #1
    case ElectricalTestWaitingFirstFail:
        electricalTestFirstErrorLoc = c;
        curState = ElectricalTestWaitingSecondFail;
        break;
    // Expecting electrical test fail location #2
    case ElectricalTestWaitingSecondFail:
        emit electricalTestFailLocation(electricalTestFirstErrorLoc, c);
        curState = ElectricalTestWaitingNextStatus;
        break;

    // READ SIMM STATE HANDLERS

    // Expecting reply after we told the programmer to start reading       
    case ReadSIMMWaitingStartReply:
    case ReadSIMMWaitingStartOffsetReply:
        switch (c)
        {
        case CommandReplyOK:

            if (!isReadVerifying)
            {
                emit readStatusChanged(ReadStarting);
            }
            else
            {
                emit writeStatusChanged(WriteVerifyStarting);
            }

            curState = ReadSIMMWaitingLengthReply;

            // Send the length requesting to be read (and offset if needed)
            if (c == ReadSIMMWaitingStartOffsetReply)
            {
                sendWord(readOffset);
            }
            sendWord(lenRemaining);

            // Now wait for the go-ahead from the programmer's side
            break;
        case CommandReplyError:
        case CommandReplyInvalid:
        default:
            curState = WaitingForNextCommand;
            closePort();
            if (!isReadVerifying)
            {
                emit readStatusChanged(ReadError);
            }
            else
            {
                // Ensure the verify buffer is empty if we were verifying
                verifyArray->clear();
                verifyBuffer->seek(0);
                emit writeStatusChanged(WriteVerifyError);
            }
            break;
        }
        break;

    // Expecting reply after we gave the programmer a length to read
    case ReadSIMMWaitingLengthReply:
        switch (c)
        {
        case ProgrammerReadOK:
            curState = ReadSIMMWaitingData;
            if (!isReadVerifying)
            {
                emit readTotalLengthChanged(lenRemaining);
                emit readCompletionLengthChanged(0);
            }
            else
            {
                emit writeVerifyTotalLengthChanged(lenRemaining);
                emit writeVerifyCompletionLengthChanged(0);
            }
            readChunkLenRemaining = READ_CHUNK_SIZE;
            break;
        case ProgrammerReadError:
        default:
            curState = WaitingForNextCommand;
            closePort();
            if (!isReadVerifying)
            {
                emit readStatusChanged(ReadError);
            }
            else
            {
                // Ensure the verify buffer is empty if we were verifying
                verifyArray->clear();
                verifyBuffer->seek(0);
                emit writeStatusChanged(WriteVerifyError);
            }
            break;
        }
        break;

    // Expecting a chunk of data back from the programmer
    case ReadSIMMWaitingData:
        // Only keep adding to the readback if we need to
        if (lenRead < trueLenToRead)
        {
            readDevice->write((const char *)&c, 1);
        }

        lenRead++;
        if (--readChunkLenRemaining == 0)
        {
            if (!isReadVerifying)
            {
                emit readCompletionLengthChanged(lenRead);
            }
            else
            {
                emit writeVerifyCompletionLengthChanged(lenRead);
            }
            qDebug() << "Received a chunk of data";
            sendByte(ComputerReadOK);
            curState = ReadSIMMWaitingStatusReply;
        }
        break;

    // Expecting status reply from programmer after we confirmed reception of
    // previous chunk of data
    case ReadSIMMWaitingStatusReply:
        switch (c)
        {
        case ProgrammerReadFinished:
            curState = WaitingForNextCommand;
            closePort();
            if (!isReadVerifying)
            {
                emit readStatusChanged(ReadComplete);
            }
            else
            {
                doVerifyAfterWriteCompare();
            }
            break;
        case ProgrammerReadConfirmCancel:
            curState = WaitingForNextCommand;
            closePort();
            if (!isReadVerifying)
            {
                emit readStatusChanged(ReadCancelled);
            }
            else
            {
                // Ensure the verify buffer is empty if we were verifying
                verifyArray->clear();
                verifyBuffer->seek(0);
                emit writeStatusChanged(WriteVerifyCancelled);
            }
            break;
        case ProgrammerReadMoreData:
            curState = ReadSIMMWaitingData;
            readChunkLenRemaining = READ_CHUNK_SIZE;
            break;
        }

        break;

    // BOOTLOADER STATE HANDLERS

    // Expecting reply after we asked for bootloader state (original request is
    // to end up in programmer mode)
    case BootloaderStateAwaitingOKReply:
        if (c == CommandReplyOK)
        {
            // Good to go, now we're waiting for the "in programmer" or "in bootloader" reply.
            curState = BootloaderStateAwaitingReply;
        }
        else
        {
            curState = WaitingForNextCommand;
            qDebug() << "Unable to enter programmer mode";
            // TODO: Error out somehow
        }
        break;

    // Expecting bootloader state after request was confirmed (original request
    // is to end up in programmer mode)
    case BootloaderStateAwaitingReply:
        switch (c)
        {
        case BootloaderStateInBootloader:
            // Oops! We're in the bootloader. Better change over to the programmer.
            qDebug() << "We're in the bootloader, so sending an \"enter programmer\" request.";
            emit startStatusChanged(ProgrammerInitializing);
            sendByte(EnterProgrammer);
            closePort();

            // Now wait for it to reconnect
            curState = BootloaderStateAwaitingUnplug;
            break;
        case BootloaderStateInProgrammer:
            // Good to go...
            // So change to the next state and send out the next command
            // to begin whatever sequence of events we expected.
            qDebug() << "Already in programmer. Good! Do the command now...";
            emit startStatusChanged(ProgrammerInitialized);
            curState = nextState;
            sendByte(nextSendByte);
            break;
            // TODO: Otherwise, raise an error?
        }
        break;

    // Expecting reply after we asked for bootloader state (original request is
    // to end up in bootloader mode)
    case BootloaderStateAwaitingOKReplyToBootloader:
        if (c == CommandReplyOK)
        {
            // Good to go, now we're waiting for the "in programmer" or "in bootloader" reply.
            curState = BootloaderStateAwaitingReplyToBootloader;
        }
        else
        {
            curState = WaitingForNextCommand;
            qDebug() << "Unable to enter bootloader mode";
            // TODO: Error out somehow
        }
        break;

    // Expecting bootloader state after request was confirmed (original request
    // is to end up in bootloader mode)
    case BootloaderStateAwaitingReplyToBootloader:
        switch (c)
        {
        case BootloaderStateInProgrammer:
            // Oops! We're in the programmer. Better change over to the bootloader.
            qDebug() << "We're in the programmer, so sending an \"enter bootloader\" request.";
            emit startStatusChanged(ProgrammerInitializing);
            sendByte(EnterBootloader);
            closePort();

            // Now wait for it to reconnect
            curState = BootloaderStateAwaitingUnplugToBootloader;
            break;
        case BootloaderStateInBootloader:
            // Good to go...
            // So change to the next state and send out the next command
            // to begin whatever sequence of events we expected.
            qDebug() << "Already in bootloader. Good! Do the command now...";
            emit startStatusChanged(ProgrammerInitialized);
            curState = nextState;
            sendByte(nextSendByte);
            break;
            // TODO: Otherwise, raise an error?
        }
        break;

    // IDENTIFICATION STATE HANDLERS

    // // Expecting reply after we told the programmer what size of SIMM to use
    case IdentificationWaitingSetSizeReply:
        switch (c)
        {
        case CommandReplyOK:
            // If we got an OK reply, we're ready to go, so start...
            sendByte(IdentifyChips);
            curState = IdentificationAwaitingOKReply;
            break;
        case CommandReplyInvalid:
        case CommandReplyError:
            // If we got an error reply, we MAY still be OK unless we were
            // requesting the large SIMM type, in which case the firmware
            // doesn't support the large SIMM type so the user needs to know.
	    if (SIMMChip() != SIMM_PLCC_x8)
            {
                // Uh oh -- this is an old firmware that doesn't support a big
                // SIMM. Let the caller know that the programmer board needs a
                // firmware update.
                qDebug() << "Programmer board needs firmware update.";
                curState = WaitingForNextCommand;
                closePort();
                emit identificationStatusChanged(IdentificationNeedsFirmwareUpdate);
            }
            else
            {
                // Error reply, but we're identifying a small SIMM, so the firmware
                // doesn't need updating -- it just didn't know how to handle
                // the "set size" command. But that's OK -- it only supports
                // the size we requested, so nothing's wrong.
                sendByte(IdentifyChips);
                curState = IdentificationAwaitingOKReply;
            }
            break;
        }
        break;

    // Expecting reply after we asked to identify chips
    case IdentificationAwaitingOKReply:
        if (c == CommandReplyOK)
        {
            // Good to go, now waiting for identification data
            emit identificationStatusChanged(IdentificationStarting);
            curState = IdentificationWaitingData;
            identificationCounter = 0;
        }
        else
        {
            // Error -- close the port, we're done!
            closePort();
            emit identificationStatusChanged(IdentificationError);
            curState = WaitingForNextCommand;
        }
        break;

    // Expecting device/manufacturer info about the chips
    case IdentificationWaitingData:
        if (identificationCounter & 1) // device ID?
        {
            chipDeviceIDs[identificationCounter/2] = c;
        }
        else // manufacturer ID?
        {
            chipManufacturerIDs[identificationCounter/2] = c;
        }

        // All done?
        if (++identificationCounter >= 8)
        {
            curState = IdentificationAwaitingDoneReply;
        }
        break;

    // Expecting final done confirmation after receiving all device/manufacturer info
    case IdentificationAwaitingDoneReply:
        curState = WaitingForNextCommand;
        closePort();
        if (c == ProgrammerIdentifyDone)
        {
            emit identificationStatusChanged(IdentificationComplete);
        }
        else
        {
            emit identificationStatusChanged(IdentificationError);
        }
        break;

    // WRITE BOOTLOADER PROGRAM STATE HANDLERS

    // Expecting reply after we asked to flash the firmware
    case BootloaderEraseProgramAwaitingStartOKReply:
        if (c == CommandReplyOK)
        {
            emit firmwareFlashStatusChanged(FirmwareFlashStarting);
            sendByte(ComputerBootloaderWriteMore);
            curState = BootloaderEraseProgramWaitingWriteMoreReply;
        }
        else
        {
            curState = WaitingForNextCommand;
            closePort();
            firmwareFile->close();
            emit firmwareFlashStatusChanged(FirmwareFlashError);
        }
        break;

    // Expecting reply after we told bootloader we're done flashing firmware
    case BootloaderEraseProgramWaitingFinishReply:
        if (c == BootloaderWriteOK)
        {
            curState = WaitingForNextCommand;
            closePort();
            firmwareFile->close();
            emit firmwareFlashStatusChanged(FirmwareFlashComplete);
        }
        else
        {
            curState = WaitingForNextCommand;
            closePort();
            firmwareFile->close();
            emit firmwareFlashStatusChanged(FirmwareFlashError);
        }
        break;

    // Expecting reply after we asked to write more firmware data
    case BootloaderEraseProgramWaitingWriteMoreReply:
        if (c == BootloaderWriteOK)
        {
            // Send the next chunk of data
            qDebug() << "Bootloader replied OK to send 1024 bytes of data! Sending...";
            int chunkSize = FIRMWARE_CHUNK_SIZE;
            if (firmwareLenRemaining < FIRMWARE_CHUNK_SIZE)
            {
                chunkSize = firmwareLenRemaining;
            }

            // Read the chunk from the file!
            QByteArray thisChunk = firmwareFile->read(chunkSize);

            // If it isn't FIRMWARE_CHUNK_SIZE, pad the rest with 0xFF
            // (unprogrammed bytes)
            for (int x = firmwareLenRemaining; x < FIRMWARE_CHUNK_SIZE; x++)
            {
                thisChunk.append(0xFF);
            }

            // Write the chunk out (it's asynchronous so will return immediately)
            serialPort->write(thisChunk);

            // OK, now we're waiting to hear back from the programmer on the result
            qDebug() << "Waiting for status reply...";
            curState = BootloaderEraseProgramWaitingWriteReply;
            firmwareLenRemaining -= chunkSize;
            firmwareLenWritten += chunkSize;
            emit firmwareFlashCompletionLengthChanged(firmwareLenWritten);
        }
        else
        {
            curState = WaitingForNextCommand;
            closePort();
            firmwareFile->close();
            emit firmwareFlashStatusChanged(FirmwareFlashError);
        }
        break;

    // Expecting reply after we sent a chunk of firmware data
    case BootloaderEraseProgramWaitingWriteReply:
        if (c == CommandReplyOK)
        {
            // Either ask to send the next chunk, or send a "finish" response
            if (firmwareLenRemaining > 0)
            {
                sendByte(ComputerBootloaderWriteMore);
                curState = BootloaderEraseProgramWaitingWriteMoreReply;
            }
            else
            {
                sendByte(ComputerBootloaderFinish);
                curState = BootloaderEraseProgramWaitingFinishReply;
            }
        }
        else
        {
            curState = WaitingForNextCommand;
            closePort();
            firmwareFile->close();
            emit firmwareFlashStatusChanged(FirmwareFlashError);
        }
        break;

    // UNUSED STATE HANDLERS (They are handled elsewhere)
    case BootloaderStateAwaitingPlug:
    case BootloaderStateAwaitingUnplug:
    case BootloaderStateAwaitingPlugToBootloader:
    case BootloaderStateAwaitingUnplugToBootloader:
        break;
    }
}

void Programmer::runElectricalTest()
{
    startProgrammerCommand(DoElectricalTest, ElectricalTestWaitingStartReply);
}

QString Programmer::electricalTestPinName(uint8_t index)
{
    if (index <= LAST_ADDRESS_LINE_FAIL_INDEX)
    {
        return QString("A%1").arg(index - FIRST_ADDRESS_LINE_FAIL_INDEX);
    }
    else if (index <= LAST_DATA_LINE_FAIL_INDEX)
    {
        // The byte ordering is backwards to the labeling, so I have to fix that.
        // Reverse the byte ordering so we have the correct number in terms of how
        // D0 to D31 are labeled...
        index = index - FIRST_DATA_LINE_FAIL_INDEX;
        if (index < 8)
        {
            index = index + 24;
        }
        else if (index < 16)
        {
            index = index + 8;
        }
        else if (index < 24)
        {
            index = index - 8;
        }
        else
        {
            index = index - 24;
        }
        return QString("D%1").arg(index);
    }
    else if (index == CS_FAIL_INDEX)
    {
        return "CS";
    }
    else if (index == OE_FAIL_INDEX)
    {
        return "OE";
    }
    else if (index == WE_FAIL_INDEX)
    {
        return "WE";
    }
    else if (index == GROUND_FAIL_INDEX)
    {
        return "GND";
    }
    else if (index == VCC_FAIL_INDEX)
    {
        return "+5V";
    }
    else
    {
        return "?";
    }
}

void Programmer::identifySIMMChips()
{
    //startProgrammerCommand(IdentifyChips, IdentificationAwaitingOKReply);
    // Based on the SIMM type, tell the programmer board.
    uint8_t setLayoutCommand;
    if (SIMMChip() == SIMM_TSOP_x8)
    {
        setLayoutCommand = SetSIMMLayout_AddressShifted;
    }
    else
    {
        setLayoutCommand = SetSIMMLayout_AddressStraight;
    }
    startProgrammerCommand(setLayoutCommand, IdentificationWaitingSetSizeReply);
}

void Programmer::getChipIdentity(int chipIndex, uint8_t *manufacturer, uint8_t *device)
{
    if ((chipIndex >= 0) && (chipIndex < 4))
    {
        *manufacturer = chipManufacturerIDs[chipIndex];
        *device = chipDeviceIDs[chipIndex];
    }
    else
    {
        *manufacturer = 0;
        *device = 0;
    }
}

void Programmer::flashFirmware(QString filename)
{
    firmwareFile = new QFile(filename);
    if (!firmwareFile->open(QFile::ReadOnly))
    {
        curState = WaitingForNextCommand;
        emit firmwareFlashStatusChanged(FirmwareFlashError);
        return;
    }

    firmwareLenWritten = 0;
    firmwareLenRemaining = firmwareFile->size();
    emit firmwareFlashTotalLengthChanged(firmwareLenRemaining);
    emit firmwareFlashCompletionLengthChanged(firmwareLenWritten);

    startBootloaderCommand(BootloaderEraseAndWriteProgram, BootloaderEraseProgramAwaitingStartOKReply);
}

// Begins a command by opening the serial port, making sure we're in the PROGRAMMER
// rather than the bootloader, then sending a command and setting a new command state.
// TODO: When it fails, this needs to carry errors over somehow.
// newState is really just a ProgrammerCommandState but in order to keep
// ProgrammerCommandState private, I did it this way.
void Programmer::startProgrammerCommand(uint8_t commandByte, uint32_t newState)
{
    nextState = (ProgrammerCommandState)newState;
    nextSendByte = commandByte;

    curState = BootloaderStateAwaitingOKReply;
    openPort();
    sendByte(GetBootloaderState);
}

// Begins a command by opening the serial port, making sure we're in the BOOTLOADER
// rather than the programmer, then sending a command and setting a new command state.
// TODO: When it fails, this needs to carry errors over somehow.
// newState is really just a ProgrammerCommandState but in order to keep
// ProgrammerCommandState private, I did it this way.
void Programmer::startBootloaderCommand(uint8_t commandByte, uint32_t newState)
{
    nextState = (ProgrammerCommandState)newState;
    nextSendByte = commandByte;

    curState = BootloaderStateAwaitingOKReplyToBootloader;
    openPort();
    sendByte(GetBootloaderState);
}

#include <QTimer>
void Programmer::portDiscovered(const QextPortInfo &info)
{
    if ((foundState == ProgrammerBoardNotFound) &&
        (info.vendorID == PROGRAMMER_USB_VENDOR_ID) &&
        (info.productID == PROGRAMMER_USB_DEVICE_ID) &&
        (info.portName != ""))
    {
        // Note: I check that portName != "" because QextSerialEnumerator seems to give me
        // 2 notifications that match the vendor ID -- one is the real deal, and the other
        // has a blank port name. If I match on the blank port name one, it breaks.

#ifdef Q_WS_WIN
        programmerBoardPortName = "\\\\.\\" + info.portName;
#else
        programmerBoardPortName = info.portName;
#endif
        foundState = ProgrammerBoardFound;

        // I create a temporary timer here because opening it immediately seems to crash
        // Mac OS X in my limited testing. Don't worry about a memory leak -- the
        // portDiscovered_internal() slot will delete the newly-allocated QTimer.
        QTimer *t = new QTimer();
        connect(t, SIGNAL(timeout()), SLOT(portDiscovered_internal()));
        t->setInterval(50);
        t->setSingleShot(true);
        t->start();
    }
}

void Programmer::portDiscovered_internal()
{
    // Delete the QTimer that sent us this signal. Ugly, but it works...
    sender()->deleteLater();

    closePort();
    serialPort->setPortName(programmerBoardPortName);

    // Don't show the "control" screen if we intentionally
    // reconnected the USB port because we are changing from bootloader
    // to programmer mode or vice-versa.
    if (curState == BootloaderStateAwaitingPlug)
    {
        openPort();
        curState = nextState;
        sendByte(nextSendByte);
    }
    else if (curState == BootloaderStateAwaitingPlugToBootloader)
    {
        openPort();
        curState = nextState;
        sendByte(nextSendByte);
    }
    else
    {
        emit programmerBoardConnected();
    }
}

void Programmer::portRemoved(const QextPortInfo &info)
{
    const bool matchingVIDPID = info.vendorID == PROGRAMMER_USB_VENDOR_ID && info.productID == PROGRAMMER_USB_DEVICE_ID;
    const bool matchingPortName = programmerBoardPortName != "" && info.portName == programmerBoardPortName;
    if ((matchingVIDPID || matchingPortName) &&
        (foundState == ProgrammerBoardFound))
    {       
        programmerBoardPortName = "";
        foundState = ProgrammerBoardNotFound;

        // Don't show the "no programmer connected" screen if we intentionally
        // disconnected the USB port because we are changing from bootloader
        // to programmer mode or vice-versa.
        if (curState == BootloaderStateAwaitingUnplug)
        {
            curState = BootloaderStateAwaitingPlug;
        }
        else if (curState == BootloaderStateAwaitingUnplugToBootloader)
        {
            curState = BootloaderStateAwaitingPlugToBootloader;
        }
        else
        {
            closePort();

            if (curState != WaitingForNextCommand)
            {
                // This means they unplugged while we were in the middle
                // of an operation. Reset state, and let them know.
                curState = WaitingForNextCommand;
                emit programmerBoardDisconnectedDuringOperation();
            }
            else
            {
                emit programmerBoardDisconnected();
            }
        }
    }
}

void Programmer::startCheckingPorts()
{
    QextSerialEnumerator *p = new QextSerialEnumerator();
    connect(p, SIGNAL(deviceDiscovered(QextPortInfo)), SLOT(portDiscovered(QextPortInfo)));
    connect(p, SIGNAL(deviceRemoved(QextPortInfo)), SLOT(portRemoved(QextPortInfo)));
    p->setUpNotifications();
}

void Programmer::openPort()
{
    serialPort->open(QextSerialPort::ReadWrite);
}

void Programmer::closePort()
{
    serialPort->close();
}

void Programmer::setSIMMType(uint32_t bytes, uint32_t chip_type)
{
    _simmCapacity = bytes;
    _simmChip = chip_type;
}

uint32_t Programmer::SIMMCapacity() const
{
    return _simmCapacity;
}

uint32_t Programmer::SIMMChip() const
{
    return _simmChip;
}

void Programmer::setVerifyMode(VerificationOption mode)
{
    _verifyMode = mode;
}

VerificationOption Programmer::verifyMode() const
{
    return _verifyMode;
}

void Programmer::doVerifyAfterWriteCompare()
{
    // Do the comparison, emit the correct signal

    // Read the entire file we just wrote into a QByteArray
    writeDevice->seek(readOffset);
    QByteArray originalFileContents = writeDevice->read(verifyLength);
    qDebug() << "Read" << originalFileContents.length() << "bytes, asked for" << verifyLength;

    WriteStatus emitStatus;

    // Now, compare the readback (but only for the length of originalFileContents
    // (because the readback might be longer since it has to be a multiple of
    // READ_CHUNK_SIZE)
    if (originalFileContents.size() <= verifyArray->size())
    {
        const char *fileBytesPtr = originalFileContents.constData();
        const char *readBytesPtr = verifyArray->constData();

        if (memcmp(fileBytesPtr, readBytesPtr, originalFileContents.size()) != 0)
        {
            // Now let's do some trickery and figure out which chip is acting up (or chips)
            _verifyBadChipMask = 0;

            // Keep a list of which chips are reading bad data back
            for (int x = 0; (x < originalFileContents.size()) && (_verifyBadChipMask != 0xF); x++)
            {
                if (fileBytesPtr[x] != readBytesPtr[x])
                {
                    // OK, we found a mismatched byte. Now look at
                    // which byte (0-3) it is in each 4-byte group.
                    // If it's byte 0, it's the MOST significant byte
                    // because the 68k is big endian. IC4 contains the
                    // MSB, so IC4 is the first chip, IC3 second, and
                    // so on. That's why I subtract it from 3 --
                    // 0 through 3 get mapped to 3 through 0.
                    _verifyBadChipMask |= (1 << (3 - (x % 4)));
                }
            }

            // Now make sure we're not complaining about chips we didn't
            // write to...this will zero out errors on chips we weren't
            // flashing, but will leave errors intact for chips we did write.
            // (the chip mask is backwards from the IC numbering...that's why
            // I have to do this in a special way)
            if ((writeChipMask & 0x01) == 0) _verifyBadChipMask &= ~0x08;
            if ((writeChipMask & 0x02) == 0) _verifyBadChipMask &= ~0x04;
            if ((writeChipMask & 0x04) == 0) _verifyBadChipMask &= ~0x02;
            if ((writeChipMask & 0x08) == 0) _verifyBadChipMask &= ~0x01;
            if (_verifyBadChipMask != 0)
            {
                emitStatus = WriteVerificationFailure;
            }
            else
            {
                emitStatus = WriteCompleteVerifyOK;
            }
        }
        else
        {
            emitStatus = WriteCompleteVerifyOK;
        }
    }
    else
    {
        // Wrong amount of data read back for some reason...shouldn't ever happen,
        // but I'll call it a verification failure.
        emitStatus = WriteVerificationFailure;
    }

    // Reset verification buffer to emptiness
    verifyArray->clear();
    verifyBuffer->seek(0);

    // Finally, emit the final status signal
    emit writeStatusChanged(emitStatus);
}
