#include "programmer.h"
#include <QDebug>
#include <QWaitCondition>
#include <QMutex>

typedef enum ProgrammerCommandState
{
    WaitingForNextCommand = 0,

    WriteSIMMWaitingEraseReply,
    WriteSIMMWaitingWriteReply,
    WriteSIMMWaitingFinishReply,
    WriteSIMMWaitingWriteMoreReply,

    ElectricalTestWaitingStartReply,
    ElectricalTestWaitingNextStatus,
    ElectricalTestWaitingFirstFail,
    ElectricalTestWaitingSecondFail,

    ReadSIMMWaitingStartReply,
    ReadSIMMWaitingData,
    ReadSIMMWaitingStatusReply,

    BootloaderStateAwaitingOKReply,
    BootloaderStateAwaitingReply,
    BootloaderStateAwaitingOKReplyToBootloader,
    BootloaderStateAwaitingReplyToBootloader,

    IdentificationAwaitingOKReply,
    IdentificationWaitingData,
    IdentificationAwaitingDoneReply,

    BootloaderEraseProgramAwaitingStartOKReply,
    BootloaderEraseProgramWaitingFinishReply,
    BootloaderEraseProgramWaitingWriteMoreReply,
    BootloaderEraseProgramWaitingWriteReply
} ProgrammerCommandState;

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
    BootloaderEraseAndWriteProgram
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
    ProgrammerWriteConfirmCancel
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





#define WRITE_CHUNK_SIZE    1024
#define READ_CHUNK_SIZE     1024
#define FIRMWARE_CHUNK_SIZE 1024

static ProgrammerCommandState curState = WaitingForNextCommand;

// After identifying that we're in the main program, what will be the command
// we will send and the state we will be waiting in?
static ProgrammerCommandState nextState = WaitingForNextCommand;
static uint8_t nextSendByte = 0;

Programmer::Programmer(QObject *parent) :
    QObject(parent)
{
    serialPort = new QextSerialPort("\\\\.\\COM13", QextSerialPort::EventDriven);
    connect(serialPort, SIGNAL(readyRead()), SLOT(dataReady()));
}

Programmer::~Programmer()
{
    if (serialPort->isOpen())
    {
        serialPort->close();
    }
    delete serialPort;
}

void Programmer::ReadSIMMToFile(QString filename)
{
    readFile = new QFile(filename);
    readFile->open(QFile::WriteOnly);
    lenRead = 0;

    emit readTotalLengthChanged(2 * 1024 * 1024);
    emit readCompletionLengthChanged(lenRead);

    startProgrammerCommand(ReadChips, ReadSIMMWaitingStartReply);
}

void Programmer::WriteFileToSIMM(QString filename)
{
    writeFile = new QFile(filename);
    if (!writeFile->open(QFile::ReadOnly))
    {
        curState = WaitingForNextCommand;
        emit writeStatusChanged(WriteError);
        return;
    }
    lenWritten = 0;
    writeLenRemaining = writeFile->size();
    emit writeTotalLengthChanged(writeLenRemaining);
    emit writeCompletionLengthChanged(lenWritten);

    startProgrammerCommand(EraseChips, WriteSIMMWaitingEraseReply);
}

void Programmer::sendByte(uint8_t b)
{
    serialPort->write((const char *)&b, 1);
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
    case WriteSIMMWaitingEraseReply:
    {
        switch (c)
        {
        case CommandReplyOK:
            sendByte(WriteChips);
            curState = WriteSIMMWaitingWriteReply;
            qDebug() << "Chips erased. Now asking to start writing...";
            emit writeStatusChanged(WriteEraseComplete);
            break;
        case CommandReplyError:
            qDebug() << "Error erasing chips.";
            writeFile->close();
            curState = WaitingForNextCommand;
            emit writeStatusChanged(WriteEraseFailed);
            serialPort->close();
            break;
        }
        break;
    }
    case WriteSIMMWaitingWriteReply:
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
            writeFile->close();
            curState = WaitingForNextCommand;
            emit writeStatusChanged(WriteError);
            serialPort->close();
            break;
        }

        break;
    case WriteSIMMWaitingWriteMoreReply:
    {
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
            QByteArray thisChunk = writeFile->read(chunkSize);

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
            writeFile->close();
            curState = WaitingForNextCommand;
            emit writeStatusChanged(WriteError);
            serialPort->close();
            break;
        }
        break;
    }
    case WriteSIMMWaitingFinishReply:
        switch (c)
        {
        case ProgrammerWriteOK:
            writeFile->close();
            curState = WaitingForNextCommand;
            qDebug() << "Write success at end";
            emit writeStatusChanged(WriteComplete);
            serialPort->close();
            break;
        case ProgrammerWriteError:
        default:
            qDebug() << "Write failure at end";
            curState = WaitingForNextCommand;
            writeFile->close();
            emit writeStatusChanged(WriteError);
            serialPort->close();
            break;
        }

        break;

    // ELECTRICAL TEST STATE HANDLERS

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
            emit electricalTestStatusChanged(ElectricalTestCouldntStart);
            serialPort->close();
        }
        break;
    case ElectricalTestWaitingNextStatus:
        switch (c)
        {
        case ProgrammerElectricalTestDone:
            if (electricalTestErrorCounter > 0)
            {
                emit electricalTestStatusChanged(ElectricalTestFailed);
            }
            else
            {
                emit electricalTestStatusChanged(ElectricalTestPassed);
            }
            curState = WaitingForNextCommand;
            serialPort->close();
            break;
        case ProgrammerElectricalTestFail:
            electricalTestErrorCounter++;
            curState = ElectricalTestWaitingFirstFail;
            break;
        }
        break;
    case ElectricalTestWaitingFirstFail:
        electricalTestFirstErrorLoc = c;
        curState = ElectricalTestWaitingSecondFail;
        break;
    case ElectricalTestWaitingSecondFail:
        emit electricalTestFailLocation(electricalTestFirstErrorLoc, c);
        curState = ElectricalTestWaitingNextStatus;
        break;

    // READ SIMM STATE HANDLERS
    case ReadSIMMWaitingStartReply:
        emit readStatusChanged(ReadStarting);
        curState = ReadSIMMWaitingData;
        readChunkLenRemaining = READ_CHUNK_SIZE;
        break;
    case ReadSIMMWaitingData:
        readFile->write((const char *)&c, 1);
        lenRead++;
        if (--readChunkLenRemaining == 0)
        {
            emit readCompletionLengthChanged(lenRead);
            qDebug() << "Received a chunk of data";
            sendByte(ComputerReadOK);
            curState = ReadSIMMWaitingStatusReply;
        }
        break;
    case ReadSIMMWaitingStatusReply:
        switch (c)
        {
        case ProgrammerReadFinished:
            curState = WaitingForNextCommand;
            serialPort->close();
            readFile->close();
            emit readStatusChanged(ReadComplete);
            break;
        case ProgrammerReadConfirmCancel:
            curState = WaitingForNextCommand;
            serialPort->close();
            readFile->close();
            emit readStatusChanged(ReadCancelled);
            break;
        case ProgrammerReadMoreData:
            curState = ReadSIMMWaitingData;
            readChunkLenRemaining = READ_CHUNK_SIZE;
            break;
        }

        break;

    // BOOTLOADER STATE HANDLERS
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
    case BootloaderStateAwaitingReply:
        switch (c)
        {
        case BootloaderStateInBootloader:
            // Oops! We're in the bootloader. Better change over to the programmer.
            // TODO: Send "enter programmer" command.
            //       Close serial port.
            //       Wait for serial port to reappear (or just wait a fixed time?)
            //       Open serial port.
            //       Ensure we're in the programmer?
            //       Then do the command correctly
            qDebug() << "We're in the bootloader, so sending an \"enter programmer\" request.";
            emit startStatusChanged(ProgrammerInitializing);
            sendByte(EnterProgrammer);
            serialPort->close();
            {
                QMutex mutex;
                mutex.lock();

                QWaitCondition waitCondition;
                waitCondition.wait(&mutex, 5000);
                mutex.unlock();
            }
            serialPort->open(QextSerialPort::ReadWrite);
            curState = nextState;
            sendByte(nextSendByte);
            // Special case: Send out notification we are starting an erase command.
            // I don't have any hooks into the process between now and the erase reply.
            if (nextSendByte == EraseChips)
            {
                emit writeStatusChanged(WriteErasing);
            }
            break;
        case BootloaderStateInProgrammer:
            // Good to go...
            // So change to the next state and send out the next command
            // to begin whatever sequence of events we expected.
            qDebug() << "Already in programmer. Good! Do the command now...";
            emit startStatusChanged(ProgrammerInitialized);
            curState = nextState;
            sendByte(nextSendByte);
            // Special case: Send out notification we are starting an erase command.
            // I don't have any hooks into the process between now and the erase reply.
            if (nextSendByte == EraseChips)
            {
                emit writeStatusChanged(WriteErasing);
            }
            break;
            // TODO: Otherwise, raise an error?
        }
        break;

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
    case BootloaderStateAwaitingReplyToBootloader:
        switch (c)
        {
        case BootloaderStateInProgrammer:
            // Oops! We're in the programmer. Better change over to the bootloader.
            // TODO: Send "enter bootloader" command.
            //       Close serial port.
            //       Wait for serial port to reappear (or just wait a fixed time?)
            //       Open serial port.
            //       Ensure we're in the bootloader?
            //       Then do the command correctly
            qDebug() << "We're in the programmer, so sending an \"enter bootloader\" request.";
            emit startStatusChanged(ProgrammerInitializing);
            sendByte(EnterBootloader);
            serialPort->close();
            {
                QMutex mutex;
                mutex.lock();

                QWaitCondition waitCondition;
                waitCondition.wait(&mutex, 5000);
                mutex.unlock();
            }
            serialPort->open(QextSerialPort::ReadWrite);
            curState = nextState;
            sendByte(nextSendByte);
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
            emit identificationStatusChanged(IdentificationError);
            curState = WaitingForNextCommand;
        }
        break;
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
    case IdentificationAwaitingDoneReply:
        if (c == ProgrammerIdentifyDone)
        {
            emit identificationStatusChanged(IdentificationComplete);
        }
        else
        {
            emit identificationStatusChanged(IdentificationError);
        }
        curState = WaitingForNextCommand;
        break;

    // WRITE BOOTLOADER PROGRAM STATE HANDLERS
    case BootloaderEraseProgramAwaitingStartOKReply:
        if (c == CommandReplyOK)
        {
            emit firmwareFlashStatusChanged(FirmwareFlashStarting);
            sendByte(ComputerBootloaderWriteMore);
            curState = BootloaderEraseProgramWaitingWriteMoreReply;
        }
        else
        {
            emit firmwareFlashStatusChanged(FirmwareFlashError);
            curState = WaitingForNextCommand;
            serialPort->close();
            firmwareFile->close();
        }
        break;
    case BootloaderEraseProgramWaitingFinishReply:
        if (c == BootloaderWriteOK)
        {
            emit firmwareFlashStatusChanged(FirmwareFlashComplete);
            curState = WaitingForNextCommand;
            serialPort->close();
            firmwareFile->close();
        }
        else
        {
            emit firmwareFlashStatusChanged(FirmwareFlashError);
            curState = WaitingForNextCommand;
            serialPort->close();
            firmwareFile->close();
        }
        break;
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
            emit firmwareFlashStatusChanged(FirmwareFlashError);
            curState = WaitingForNextCommand;
            serialPort->close();
            firmwareFile->close();
        }
        break;
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
            emit firmwareFlashStatusChanged(FirmwareFlashError);
            curState = WaitingForNextCommand;
            serialPort->close();
            firmwareFile->close();
        }
        break;
    }
}

void Programmer::RunElectricalTest()
{
    startProgrammerCommand(DoElectricalTest, ElectricalTestWaitingStartReply);
}

QString Programmer::electricalTestPinName(uint8_t index)
{
    if (index <= LAST_ADDRESS_LINE_FAIL_INDEX)
    {
        return QString().sprintf("A%d", index - FIRST_ADDRESS_LINE_FAIL_INDEX);
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
        return QString().sprintf("D%d", index);
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
    else
    {
        return "?";
    }
}

void Programmer::IdentifySIMMChips()
{
    startProgrammerCommand(IdentifyChips, IdentificationAwaitingOKReply);
}

void Programmer::GetChipIdentity(int chipIndex, uint8_t *manufacturer, uint8_t *device)
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

void Programmer::FlashFirmware(QString filename)
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
    serialPort->open(QextSerialPort::ReadWrite);
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
    serialPort->open(QextSerialPort::ReadWrite);
    sendByte(GetBootloaderState);
}
