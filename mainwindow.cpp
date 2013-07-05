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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "programmer.h"
#include "aboutbox.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QSettings>

static Programmer *p;

#define selectedCapacityKey     "selectedCapacity"
#define verifyAfterWriteKey     "verifyAfterWrite"
#define verifyWhileWritingKey   "verifyWhileWriting"
#define selectedEraseSizeKey    "selectedEraseSize"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    writeFile(NULL),
    readFile(NULL),
    writeBuffer(NULL),
    readBuffer(NULL)
{
    initializing = true;
    // Make default QSettings use these settings
    QCoreApplication::setOrganizationName("Doug Brown");
    QCoreApplication::setOrganizationDomain("downtowndougbrown.com");
    QCoreApplication::setApplicationName("SIMMProgrammer");
    QSettings settings;

    p = new Programmer();
    ui->setupUi(this);
    hideFlashIndividualControls();
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->actionUpdate_firmware->setEnabled(false);

    // Fill in the list of SIMM chip capacities (programmer can support anywhere up to 8 MB of space)
    ui->simmCapacityBox->addItem("32 KB (256 Kb) per chip * 4 chips = 128 KB", QVariant(128 * 1024));
    ui->simmCapacityBox->addItem("64 KB (512 Kb) per chip * 4 chips = 256 KB", QVariant(256 * 1024));
    ui->simmCapacityBox->addItem("128 KB (1 Mb) per chip * 4 chips = 512 KB", QVariant(512 * 1024));
    ui->simmCapacityBox->addItem("256 KB (2 Mb) per chip * 4 chips = 1 MB", QVariant(1 * 1024 * 1024));
    ui->simmCapacityBox->addItem("512 KB (4 Mb) per chip * 4 chips = 2 MB", QVariant(2 * 1024 * 1024));
    ui->simmCapacityBox->addItem("1 MB (8 Mb) per chip * 4 chips = 4 MB", QVariant(4 * 1024 * 1024));
    ui->simmCapacityBox->addItem("2 MB (16 Mb) per chip * 4 chips = 8 MB", QVariant(8 * 1024 * 1024));

    // Select 2 MB by default (it's what most people will want), or load last-used setting
    QVariant selectedCapacity = settings.value(selectedCapacityKey, QVariant(2 * 1024 * 1024));
    int selectedIndex = ui->simmCapacityBox->findData(selectedCapacity);
    if (selectedIndex != -1)
    {
        ui->simmCapacityBox->setCurrentIndex(selectedIndex);
    }

    // Fill in the list of verification options
    ui->verifyBox->addItem("Don't verify", QVariant(NoVerification));
    ui->verifyBox->addItem("Verify while writing", QVariant(VerifyWhileWriting));
    ui->verifyBox->addItem("Verify after writing", QVariant(VerifyAfterWrite));

    // Decide whether to verify while writing, after writing, or never.
    // This would probably be better suited as an enum rather than multiple bools,
    // but I started out with it as a single bool, so for backward compatibility,
    // I simply added another bool for the "verify while writing" capability.
    bool verifyAfterWrite = settings.value(verifyAfterWriteKey, false).toBool();
    bool verifyWhileWriting = settings.value(verifyWhileWritingKey, true).toBool();
    selectedIndex = 0;
    if (verifyWhileWriting)
    {
        selectedIndex = ui->verifyBox->findData(VerifyWhileWriting);
    }
    else if (verifyAfterWrite)
    {
        selectedIndex = ui->verifyBox->findData(VerifyAfterWrite);
    }
    else
    {
        selectedIndex = ui->verifyBox->findData(NoVerification);
    }

    if (selectedIndex != -1)
    {
        ui->verifyBox->setCurrentIndex(selectedIndex);
    }

    // Fill in list of "write first xxx bytes" options
    ui->howMuchToWriteBox->addItem("Erase/write entire SIMM", QVariant(0));
    ui->howMuchToWriteBox->addItem("Only erase/write first 256 KB", QVariant(256*1024));
    ui->howMuchToWriteBox->addItem("Only erase/write first 512 KB", QVariant(512*1024));
    ui->howMuchToWriteBox->addItem("Only erase/write first 1 MB", QVariant(1024*1024));
    ui->howMuchToWriteBox->addItem("Only erase/write first 1.5 MB", QVariant(3*512*1024));
    ui->howMuchToWriteBox->addItem("Only erase/write first 2 MB", QVariant(2*1024*1024));

    // Select "erase entire SIMM" by default, or load last-used setting
    QVariant selectedEraseSize = settings.value(selectedEraseSizeKey, QVariant(0));
    selectedIndex = ui->howMuchToWriteBox->findData(selectedEraseSize);
    if (selectedIndex != -1)
    {
        ui->howMuchToWriteBox->setCurrentIndex(selectedIndex);
    }

    ui->chosenWriteFile->setText("");
    ui->chosenReadFile->setText("");
    writeFileValid = false;
    readFileValid = false;
    ui->writeToSIMMButton->setEnabled(false);
    ui->readFromSIMMButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->statusLabel->setText("");
    ui->cancelButton->setEnabled(false);

    connect(p, SIGNAL(writeStatusChanged(WriteStatus)), SLOT(programmerWriteStatusChanged(WriteStatus)));
    connect(p, SIGNAL(writeTotalLengthChanged(uint32_t)), SLOT(programmerWriteTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(writeCompletionLengthChanged(uint32_t)), SLOT(programmerWriteCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(writeVerifyTotalLengthChanged(uint32_t)), SLOT(programmerVerifyTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(writeVerifyCompletionLengthChanged(uint32_t)), SLOT(programmerVerifyCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(electricalTestStatusChanged(ElectricalTestStatus)), SLOT(programmerElectricalTestStatusChanged(ElectricalTestStatus)));
    connect(p, SIGNAL(electricalTestFailLocation(uint8_t,uint8_t)), SLOT(programmerElectricalTestLocation(uint8_t,uint8_t)));
    connect(p, SIGNAL(readStatusChanged(ReadStatus)), SLOT(programmerReadStatusChanged(ReadStatus)));
    connect(p, SIGNAL(readTotalLengthChanged(uint32_t)), SLOT(programmerReadTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(readCompletionLengthChanged(uint32_t)), SLOT(programmerReadCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(identificationStatusChanged(IdentificationStatus)), SLOT(programmerIdentifyStatusChanged(IdentificationStatus)));
    connect(p, SIGNAL(firmwareFlashStatusChanged(FirmwareFlashStatus)), SLOT(programmerFirmwareFlashStatusChanged(FirmwareFlashStatus)));
    connect(p, SIGNAL(firmwareFlashTotalLengthChanged(uint32_t)), SLOT(programmerFirmwareFlashTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(firmwareFlashCompletionLengthChanged(uint32_t)), SLOT(programmerFirmwareFlashCompletionLengthChanged(uint32_t)));
    connect(p, SIGNAL(programmerBoardConnected()), SLOT(programmerBoardConnected()));
    connect(p, SIGNAL(programmerBoardDisconnected()), SLOT(programmerBoardDisconnected()));
    connect(p, SIGNAL(programmerBoardDisconnectedDuringOperation()), SLOT(programmerBoardDisconnectedDuringOperation()));
    p->startCheckingPorts();

    // Set up the multi chip flasher UI -- connect signals
    connect(ui->chosenFlashIC1File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->chosenFlashIC2File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->chosenFlashIC3File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->chosenFlashIC4File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));

    connect(ui->chosenReadIC1File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->chosenReadIC2File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->chosenReadIC3File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->chosenReadIC4File, SIGNAL(textEdited(QString)), SLOT(updateFlashIndividualControlsEnabled()));

    connect(ui->flashIC1CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->flashIC2CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->flashIC3CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->flashIC4CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));

    connect(ui->readIC1CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->readIC2CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->readIC3CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));
    connect(ui->readIC4CheckBox, SIGNAL(clicked()), SLOT(updateFlashIndividualControlsEnabled()));

    connect(ui->selectFlashIC1Button, SIGNAL(clicked()), SLOT(selectIndividualWriteFileClicked()));
    connect(ui->selectFlashIC2Button, SIGNAL(clicked()), SLOT(selectIndividualWriteFileClicked()));
    connect(ui->selectFlashIC3Button, SIGNAL(clicked()), SLOT(selectIndividualWriteFileClicked()));
    connect(ui->selectFlashIC4Button, SIGNAL(clicked()), SLOT(selectIndividualWriteFileClicked()));

    connect(ui->selectReadIC1Button, SIGNAL(clicked()), SLOT(selectIndividualReadFileClicked()));
    connect(ui->selectReadIC2Button, SIGNAL(clicked()), SLOT(selectIndividualReadFileClicked()));
    connect(ui->selectReadIC3Button, SIGNAL(clicked()), SLOT(selectIndividualReadFileClicked()));
    connect(ui->selectReadIC4Button, SIGNAL(clicked()), SLOT(selectIndividualReadFileClicked()));

    initializing = false;
}

MainWindow::~MainWindow()
{
    delete p;
    delete ui;
}

void MainWindow::on_selectWriteFileButton_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a ROM image:");
    if (!filename.isNull())
    {
        writeFileValid = true;
        ui->chosenWriteFile->setText(filename);
        ui->writeToSIMMButton->setEnabled(true);
    }
}

void MainWindow::on_selectReadFileButton_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this, "Save ROM image as:");
    if (!filename.isNull())
    {
        readFileValid = true;
        ui->chosenReadFile->setText(filename);
        ui->readFromSIMMButton->setEnabled(true);
    }
}

void MainWindow::on_readFromSIMMButton_clicked()
{
    // Ensure we don't think we're in buffer writing/reading mode...we're reading to
    // an actual file.
    if (writeBuffer)
    {
        delete writeBuffer;
        writeBuffer = NULL;
    }
    if (readBuffer)
    {
        delete readBuffer;
        readBuffer = NULL;
    }
    if (readFile)
    {
        readFile->close();
        delete readFile;
    }
    readFile = new QFile(ui->chosenReadFile->text());
    if (readFile)
    {
        if (!readFile->open(QFile::WriteOnly))
        {
            delete readFile;
            readFile = NULL;
            programmerReadStatusChanged(ReadError);
            return;
        }
        resetAndShowStatusPage();
        p->readSIMM(readFile);
        qDebug() << "Reading from SIMM...";
    }
    else
    {
        programmerReadStatusChanged(ReadError);
    }
}

void MainWindow::on_writeToSIMMButton_clicked()
{
    // Ensure we don't think we're in buffer writing/reading mode...we're writing
    // an actual file.
    if (writeBuffer)
    {
        delete writeBuffer;
        writeBuffer = NULL;
    }
    if (readBuffer)
    {
        delete readBuffer;
        readBuffer = NULL;
    }
    if (writeFile)
    {
        writeFile->close();
        delete writeFile;
    }
    writeFile = new QFile(ui->chosenWriteFile->text());
    if (writeFile)
    {
        if (!writeFile->open(QFile::ReadOnly))
        {
            delete writeFile;
            writeFile = NULL;
            programmerWriteStatusChanged(WriteError);
            return;
        }
        resetAndShowStatusPage();

        uint howMuchToErase = ui->howMuchToWriteBox->itemData(ui->howMuchToWriteBox->currentIndex()).toUInt();
        if (howMuchToErase == 0)
        {
            p->writeToSIMM(writeFile);
        }
        else
        {
            p->writeToSIMM(writeFile, 0, howMuchToErase);
        }
        qDebug() << "Writing to SIMM...";
    }
    else
    {
        programmerWriteStatusChanged(WriteError);
    }
}

void MainWindow::on_chosenWriteFile_textEdited(const QString &newText)
{
    QFileInfo fi(newText);
    if (!newText.isEmpty() && fi.exists() && fi.isFile())
    {
        ui->writeToSIMMButton->setEnabled(true);
        writeFileValid = true;
    }
    else
    {
        ui->writeToSIMMButton->setEnabled(false);
        writeFileValid = false;
    }
}

void MainWindow::on_chosenReadFile_textEdited(const QString &newText)
{
    QFileInfo fi(newText);
    if (!newText.isEmpty() && fi.dir().exists())
    {
        ui->readFromSIMMButton->setEnabled(true);
        readFileValid = true;
    }
    else
    {
        ui->readFromSIMMButton->setEnabled(false);
        readFileValid = false;
    }
}

void MainWindow::programmerWriteStatusChanged(WriteStatus newStatus)
{
    switch (newStatus)
    {
    case WriteErasing:
        if (writeBuffer)
        {
            ui->statusLabel->setText("Erasing chip(s) to be flashed (this may take a few seconds)...");
        }
        else
        {
            ui->statusLabel->setText("Erasing SIMM (this may take a few seconds)...");
        }
        break;
    case WriteCompleteNoVerify:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        QMessageBox::information(this, "Write complete", "The write operation finished.");

        returnToControlPage();
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteCompleteVerifyOK:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        QMessageBox::information(this, "Write complete", "The write operation finished, and the contents were verified successfully.");

        returnToControlPage();
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteVerifying:
        resetAndShowStatusPage();
        break;
    case WriteVerifyStarting:
        ui->statusLabel->setText("Verifying SIMM contents...");
        break;
    case WriteVerifyError:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Verify error", "An error occurred reading the SIMM contents for verification.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteVerifyCancelled:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Verify cancelled", "The verify operation was cancelled.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteVerifyTimedOut:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Verify timed out", "The verify operation timed out.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteVerificationFailure:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        // The verify failure code is somewhat complicated so it's best to put
        // it elsewhere.
        handleVerifyFailureReply();
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteError:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Write error", "An error occurred writing to the SIMM.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteNeedsFirmwareUpdateBiggerSIMM:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support a larger SIMM. Please update the firmware and try again.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteNeedsFirmwareUpdateVerifyWhileWrite:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support the \"verify while writing\" capability. Please update the firmware and try again.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteNeedsFirmwareUpdateErasePortion:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support the \"erase only a portion of the SIMM\" capability. Please update the firmware and try again.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteNeedsFirmwareUpdateIndividualChips:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support the \"program individual chips\" capability. Please update the firmware and try again.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteCancelled:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Write cancelled", "The write operation was cancelled.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteEraseComplete:
        ui->statusLabel->setText("Writing SIMM...");
        break;
    case WriteEraseFailed:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Write error", "An error occurred erasing the SIMM.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteTimedOut:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Write timed out", "The write operation timed out.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteFileTooBig:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "File too big", "The file you chose to write to the SIMM is too big according to the chip size you have selected.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    case WriteEraseBlockWrongSize:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Bad erase size", "The programmer cannot handle the erase size you chose.");
        if (writeBuffer)
        {
            writeBuffer->close();
            delete writeBuffer;
            writeBuffer = NULL;
        }
        break;
    }
}

void MainWindow::programmerWriteTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerWriteCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}

void MainWindow::programmerVerifyTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerVerifyCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}

void MainWindow::on_electricalTestButton_clicked()
{
    resetAndShowStatusPage();
    electricalTestString = "";
    p->runElectricalTest();
}

void MainWindow::programmerElectricalTestStatusChanged(ElectricalTestStatus newStatus)
{
    switch (newStatus)
    {
    case ElectricalTestStarted:
        ui->statusLabel->setText("Running electrical test (this may take a few seconds)...");
        qDebug() << "Electrical test started";
        break;
    case ElectricalTestPassed:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::information(this, "Test passed", "The electrical test passed successfully.");
        break;
    case ElectricalTestFailed:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Test failed", "The electrical test failed:\n\n" + electricalTestString);
        break;
    case ElectricalTestTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Test timed out", "The electrical test operation timed out.");
        break;
    case ElectricalTestCouldntStart:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Communication error", "Unable to communicate with programmer board.");
        break;
    }
}

void MainWindow::programmerElectricalTestLocation(uint8_t loc1, uint8_t loc2)
{
    //qDebug() << "Electrical test error at (" << p->electricalTestPinName(loc1) << "," << p->electricalTestPinName(loc2) << ")";
    if (electricalTestString != "")
    {
        electricalTestString.append("\n");
    }

    electricalTestString.append(p->electricalTestPinName(loc1));
    electricalTestString.append(" shorted to ");
    electricalTestString.append(p->electricalTestPinName(loc2));
}

void MainWindow::programmerReadStatusChanged(ReadStatus newStatus)
{
    switch (newStatus)
    {
    case ReadStarting:
        ui->statusLabel->setText("Reading SIMM contents...");
        break;
    case ReadComplete:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }
        if (readBuffer)
        {
            finishMultiRead();
        }

        returnToControlPage();
        QMessageBox::information(this, "Read complete", "The read operation finished.");
        if (readBuffer)
        {
            delete readBuffer;
            readBuffer = NULL;
        }
        break;
    case ReadError:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Read error", "An error occurred reading from the SIMM.");
        if (readBuffer)
        {
            delete readBuffer;
            readBuffer = NULL;
        }
        break;
    case ReadCancelled:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Read cancelled", "The read operation was cancelled.");
        if (readBuffer)
        {
            delete readBuffer;
            readBuffer = NULL;
        }
        break;
    case ReadTimedOut:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }

        returnToControlPage();
        QMessageBox::warning(this, "Read timed out", "The read operation timed out.");
        if (readBuffer)
        {
            delete readBuffer;
            readBuffer = NULL;
        }
        break;
    }
}

void MainWindow::programmerReadTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerReadCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}

void MainWindow::programmerIdentifyStatusChanged(IdentificationStatus newStatus)
{
    switch (newStatus)
    {
    case IdentificationStarting:
        ui->statusLabel->setText("Identifying chips...");
        break;
    case IdentificationComplete:
    {
        ui->pages->setCurrentWidget(ui->controlPage);
        QString identifyString = "The chips identified themselves as:";
        for (int x = 0; x < 4; x++)
        {
            QString thisString;
            uint8_t manufacturer = 0;
            uint8_t device = 0;
            p->getChipIdentity(x, &manufacturer, &device);
            thisString.sprintf("\nIC%d: Manufacturer 0x%02X, Device 0x%02X", (x + 1), manufacturer, device);
            identifyString.append(thisString);
        }

        QMessageBox::information(this, "Identification complete", identifyString);
        break;
    }
    case IdentificationError:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Identification error", "An error occurred identifying the chips on the SIMM.");
        break;
    case IdentificationTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Identification timed out", "The identification operation timed out.");
        break;
    case IdentificationNeedsFirmwareUpdate:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support a larger SIMM. Please update the firmware and try again.");
        break;
    }
}

void MainWindow::programmerFirmwareFlashStatusChanged(FirmwareFlashStatus newStatus)
{
    switch (newStatus)
    {
    case FirmwareFlashStarting:
        ui->statusLabel->setText("Flashing new firmware...");
        break;
    case FirmwareFlashComplete:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::information(this, "Firmware update complete", "The firmware update operation finished.");
        break;
    case FirmwareFlashError:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update error", "An error occurred writing firmware to the device.");
        break;
    case FirmwareFlashCancelled:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update cancelled", "The firmware update was cancelled.");
        break;
    case FirmwareFlashTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update timed out", "The firmware update operation timed out.");
        break;
    }
}

void MainWindow::programmerFirmwareFlashTotalLengthChanged(uint32_t totalLen)
{
    ui->progressBar->setMaximum((int)totalLen);
}

void MainWindow::programmerFirmwareFlashCompletionLengthChanged(uint32_t len)
{
    ui->progressBar->setValue((int)len);
}


void MainWindow::on_actionUpdate_firmware_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a firmware image:");
    if (!filename.isNull())
    {
        resetAndShowStatusPage();
        p->flashFirmware(filename);
        qDebug() << "Updating firmware...";
    }
}

void MainWindow::on_identifyButton_clicked()
{
    resetAndShowStatusPage();
    p->identifySIMMChips();
}

void MainWindow::programmerBoardConnected()
{
    returnToControlPage();
    ui->actionUpdate_firmware->setEnabled(true);
}

void MainWindow::programmerBoardDisconnected()
{
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->actionUpdate_firmware->setEnabled(false);
}

void MainWindow::programmerBoardDisconnectedDuringOperation()
{
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->actionUpdate_firmware->setEnabled(false);
    // Make sure any files have been closed if we were in the middle of something.
    if (writeFile)
    {
        writeFile->close();
        delete writeFile;
        writeFile = NULL;
    }
    if (readFile)
    {
        readFile->close();
        delete readFile;
        readFile = NULL;
    }
    QMessageBox::warning(this, "Programmer lost connection", "Lost contact with the programmer board. Unplug it, plug it back in, and try again.");
}

void MainWindow::resetAndShowStatusPage()
{
    // Show indeterminate progress bar until communication succeeds/fails
    ui->progressBar->setRange(0, 0);
    ui->statusLabel->setText("Communicating with programmer (this may take a few seconds)...");
    ui->pages->setCurrentWidget(ui->statusPage);
}

void MainWindow::on_simmCapacityBox_currentIndexChanged(int index)
{
    uint32_t newCapacity = static_cast<uint32_t>(ui->simmCapacityBox->itemData(index).toUInt());
    p->setSIMMCapacity(newCapacity);
    QSettings settings;
    if (!initializing)
    {
        // If we're not initializing (it gets called while we're initializing),
        // go ahead and save this as the new default.
        settings.setValue(selectedCapacityKey, newCapacity);
    }
}

void MainWindow::on_verifyBox_currentIndexChanged(int index)
{
    if (index < 0) return;

    VerificationOption vo = static_cast<VerificationOption>(ui->verifyBox->itemData(index).toUInt());

    // Save this as the new default.
    QSettings settings;

    p->setVerifyMode(vo);

    if (!initializing)
    {
        if (vo == NoVerification)
        {
            settings.setValue(verifyAfterWriteKey, false);
            settings.setValue(verifyWhileWritingKey, false);
        }
        else if (vo == VerifyAfterWrite)
        {
            settings.setValue(verifyAfterWriteKey, true);
            settings.setValue(verifyWhileWritingKey, false);
        }
        else if (vo == VerifyWhileWriting)
        {
            settings.setValue(verifyAfterWriteKey, false);
            settings.setValue(verifyWhileWritingKey, true);
        }
    }
}

void MainWindow::on_howMuchToWriteBox_currentIndexChanged(int index)
{
    if (index < 0) return;

    uint32_t newEraseSize = static_cast<uint32_t>(ui->howMuchToWriteBox->itemData(index).toUInt());

    QSettings settings;
    if (!initializing)
    {
        // If we're not initializing (it gets called while we're initializing),
        // go ahead and save this as the new default.
        settings.setValue(selectedEraseSizeKey, newEraseSize);
    }
}

void MainWindow::on_actionAbout_SIMM_Programmer_triggered()
{
    AboutBox::instance()->show();
}

void MainWindow::handleVerifyFailureReply()
{
    // Make a comma-separated list of IC names from this list
    uint8_t badICMask = p->verifyBadChipMask();
    QString icList;
    bool first = true;
    for (int x = 0; x < 4; x++)
    {
        if (badICMask & (1 << x))
        {
            if (first)
            {
                // not IC0 through IC3; IC1 through IC4.
                // that's why I add one.
                icList.append(QString("IC%1").arg(x+1));
                first = false;
            }
            else
            {
                icList.append(QString(", IC%1").arg(x+1));
            }
        }
    }

    returnToControlPage();
    QMessageBox::warning(this, "Verify error", "The data read back from the SIMM did not match the data written to it. Bad data on chips: " + icList);
}

void MainWindow::on_flashIndividualEnterButton_clicked()
{
    showFlashIndividualControls();
    updateFlashIndividualControlsEnabled();
    ui->pages->setCurrentWidget(ui->flashChipsPage);
}

void MainWindow::on_returnNormalButton_clicked()
{
    hideFlashIndividualControls(); // to allow the window to be shrunk when not active
    ui->pages->setCurrentWidget(ui->controlPage);
}

void MainWindow::hideFlashIndividualControls()
{
    ui->chosenFlashIC1File->hide();
    ui->chosenFlashIC2File->hide();
    ui->chosenFlashIC3File->hide();
    ui->chosenFlashIC4File->hide();

    ui->chosenReadIC1File->hide();
    ui->chosenReadIC2File->hide();
    ui->chosenReadIC3File->hide();
    ui->chosenReadIC4File->hide();

    ui->flashIC1CheckBox->hide();
    ui->flashIC2CheckBox->hide();
    ui->flashIC3CheckBox->hide();
    ui->flashIC4CheckBox->hide();

    ui->readIC1CheckBox->hide();
    ui->readIC2CheckBox->hide();
    ui->readIC3CheckBox->hide();
    ui->readIC4CheckBox->hide();

    ui->selectFlashIC1Button->hide();
    ui->selectFlashIC2Button->hide();
    ui->selectFlashIC3Button->hide();
    ui->selectFlashIC4Button->hide();

    ui->selectReadIC1Button->hide();
    ui->selectReadIC2Button->hide();
    ui->selectReadIC3Button->hide();
    ui->selectReadIC4Button->hide();

    ui->multiFlashChipsButton->hide();
    ui->multiReadChipsButton->hide();

    ui->returnNormalButton->hide();
}

void MainWindow::showFlashIndividualControls()
{
    ui->chosenFlashIC1File->show();
    ui->chosenFlashIC2File->show();
    ui->chosenFlashIC3File->show();
    ui->chosenFlashIC4File->show();

    ui->chosenReadIC1File->show();
    ui->chosenReadIC2File->show();
    ui->chosenReadIC3File->show();
    ui->chosenReadIC4File->show();

    ui->flashIC1CheckBox->show();
    ui->flashIC2CheckBox->show();
    ui->flashIC3CheckBox->show();
    ui->flashIC4CheckBox->show();

    ui->readIC1CheckBox->show();
    ui->readIC2CheckBox->show();
    ui->readIC3CheckBox->show();
    ui->readIC4CheckBox->show();

    ui->selectFlashIC1Button->show();
    ui->selectFlashIC2Button->show();
    ui->selectFlashIC3Button->show();
    ui->selectFlashIC4Button->show();

    ui->selectReadIC1Button->show();
    ui->selectReadIC2Button->show();
    ui->selectReadIC3Button->show();
    ui->selectReadIC4Button->show();

    ui->multiFlashChipsButton->show();
    ui->multiReadChipsButton->show();

    ui->returnNormalButton->show();
}

void MainWindow::updateFlashIndividualControlsEnabled()
{
    int numWriteFilesChecked = 0;
    int numReadFilesChecked = 0;
    bool hasBadWriteFileName = false;
    bool hasBadReadFileName = false;

    QCheckBox * const flashBoxes[] = {ui->flashIC1CheckBox,
                                      ui->flashIC2CheckBox,
                                      ui->flashIC3CheckBox,
                                      ui->flashIC4CheckBox};
    QCheckBox * const readBoxes[] = {ui->readIC1CheckBox,
                                     ui->readIC2CheckBox,
                                     ui->readIC3CheckBox,
                                     ui->readIC4CheckBox};
    QPushButton * const flashSelectButtons[] = {ui->selectFlashIC1Button,
                                                ui->selectFlashIC2Button,
                                                ui->selectFlashIC3Button,
                                                ui->selectFlashIC4Button};
    QPushButton * const readSelectButtons[] = {ui->selectReadIC1Button,
                                               ui->selectReadIC2Button,
                                               ui->selectReadIC3Button,
                                               ui->selectReadIC4Button};
    QLineEdit * const flashChosenFileEdits[] = {ui->chosenFlashIC1File,
                                                ui->chosenFlashIC2File,
                                                ui->chosenFlashIC3File,
                                                ui->chosenFlashIC4File};
    QLineEdit * const readChosenFileEdits[] = {ui->chosenReadIC1File,
                                               ui->chosenReadIC2File,
                                               ui->chosenReadIC3File,
                                               ui->chosenReadIC4File};

    for (size_t x = 0; x < sizeof(flashBoxes)/sizeof(flashBoxes[0]); x++)
    {
        bool isChecked = flashBoxes[x]->isChecked();

        flashChosenFileEdits[x]->setEnabled(isChecked);
        flashSelectButtons[x]->setEnabled(isChecked);
        if (isChecked)
        {
            numWriteFilesChecked++;
            QFileInfo fi(flashChosenFileEdits[x]->text());
            if (flashChosenFileEdits[x]->text().isEmpty() || !fi.exists() || !fi.isFile())
            {
                hasBadWriteFileName = true;
            }
        }

        isChecked = readBoxes[x]->isChecked();

        readChosenFileEdits[x]->setEnabled(isChecked);
        readSelectButtons[x]->setEnabled(isChecked);
        if (isChecked)
        {
            numReadFilesChecked++;
            QFileInfo fi(readChosenFileEdits[x]->text());
            if (readChosenFileEdits[x]->text().isEmpty() || !fi.dir().exists())
            {
                hasBadReadFileName = true;
            }
        }
    }

    // All of the individual controls should be handled; now do the two main
    // buttons.
    if ((numWriteFilesChecked == 0) || hasBadWriteFileName)
    {
        ui->multiFlashChipsButton->setEnabled(false);
    }
    else
    {
        ui->multiFlashChipsButton->setEnabled(true);
    }

    if ((numReadFilesChecked == 0) || hasBadReadFileName)
    {
        ui->multiReadChipsButton->setEnabled(false);
    }
    else
    {
        ui->multiReadChipsButton->setEnabled(true);
    }
}

void MainWindow::selectIndividualWriteFileClicked()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a file to write to the chip:");
    if (!filename.isNull())
    {
        if (sender() == static_cast<QObject *>(ui->selectFlashIC1Button))
        {
            ui->chosenFlashIC1File->setText(filename);
        }
        else if (sender() == static_cast<QObject *>(ui->selectFlashIC2Button))
        {
            ui->chosenFlashIC2File->setText(filename);
        }
        else if (sender() == static_cast<QObject *>(ui->selectFlashIC3Button))
        {
            ui->chosenFlashIC3File->setText(filename);
        }
        else if (sender() == static_cast<QObject *>(ui->selectFlashIC4Button))
        {
            ui->chosenFlashIC4File->setText(filename);
        }
        updateFlashIndividualControlsEnabled();
    }
}

void MainWindow::selectIndividualReadFileClicked()
{
    QString filename = QFileDialog::getSaveFileName(this, "Save binary file as:");
    if (!filename.isNull())
    {
        if (sender() == static_cast<QObject *>(ui->selectReadIC1Button))
        {
            ui->chosenReadIC1File->setText(filename);
        }
        else if (sender() == static_cast<QObject *>(ui->selectReadIC2Button))
        {
            ui->chosenReadIC2File->setText(filename);
        }
        else if (sender() == static_cast<QObject *>(ui->selectReadIC3Button))
        {
            ui->chosenReadIC3File->setText(filename);
        }
        else if (sender() == static_cast<QObject *>(ui->selectReadIC4Button))
        {
            ui->chosenReadIC4File->setText(filename);
        }
        updateFlashIndividualControlsEnabled();
    }
}

void MainWindow::on_multiFlashChipsButton_clicked()
{
    QCheckBox * const flashBoxes[] = {ui->flashIC1CheckBox,
                                      ui->flashIC2CheckBox,
                                      ui->flashIC3CheckBox,
                                      ui->flashIC4CheckBox};

    QLineEdit * const flashChosenFileEdits[] = {ui->chosenFlashIC1File,
                                                ui->chosenFlashIC2File,
                                                ui->chosenFlashIC3File,
                                                ui->chosenFlashIC4File};

    // Read up to four files and create a combined image that we flash using
    // the standard procedure...
    if (writeBuffer)
    {
        delete writeBuffer;
    }
    writeBuffer = new QBuffer();
    qint64 maxSize = 0;
    bool hadError = false;
    uint8_t chipsMask = 0;

    // Read each file and ensure it exists. Oh, and create the mask of which
    // chips we're flashing.
    QFile *files[sizeof(flashBoxes)/sizeof(flashBoxes[0])] = {NULL, NULL, NULL, NULL};
    for (size_t x = 0; x < sizeof(files)/sizeof(files[0]); x++)
    {
        if (flashBoxes[x]->isChecked())
        {
            files[x] = new QFile(flashChosenFileEdits[x]->text());
            if (!files[x]->exists())
            {
                hadError = true;
            }
            if (files[x]->size() > maxSize)
            {
                maxSize = files[x]->size();
            }
            files[x]->open(QFile::ReadOnly);
            // Create our chip mask. chip mask is backward from IC numbering
            // (bit 0 = IC4, bit 1 = IC3, ...)
            chipsMask |= (1 << (3-x));
        }
    }

    // If there was an error or one of the files picked was too big,
    // error out.
    if (hadError || (maxSize > (p->SIMMCapacity() / 4)))
    {
        for (size_t x = 0; x < sizeof(files)/sizeof(files[0]); x++)
        {
            if (files[x]) delete files[x];
        }

        programmerWriteStatusChanged(WriteError);
        return;
    }

    // Combine the (up to four) files into a single
    // interleaved file to send to the SIMM
    writeBuffer->open(QFile::ReadWrite);
    for (int x = 0; x < maxSize; x++)
    {
        // Go in reverse order through the files so the data is saved to
        // the SIMM in the correct order
        for (int y = (int)(sizeof(files)/sizeof(files[0])) - 1; y >= 0; y--)
        {
            char c = 0xFF;
            if (files[y] && !files[y]->atEnd())
            {
                files[y]->getChar(&c);

            }
            writeBuffer->putChar(c);
        }
    }

    // Discard the temporary files
    for (size_t x = 0; x < sizeof(files)/sizeof(files[0]); x++)
    {
        if (files[x])
        {
            files[x]->close();
            delete files[x];
        }
    }

    // Now go back to the beginning of the file...
    // and write it!
    resetAndShowStatusPage();
    writeBuffer->seek(0);
    p->writeToSIMM(writeBuffer, chipsMask);
}

void MainWindow::on_multiReadChipsButton_clicked()
{
    QCheckBox * const readBoxes[] = {ui->readIC1CheckBox,
                                     ui->readIC2CheckBox,
                                     ui->readIC3CheckBox,
                                     ui->readIC4CheckBox};

    QLineEdit * const readChosenFileEdits[] = {ui->chosenReadIC1File,
                                               ui->chosenReadIC2File,
                                               ui->chosenReadIC3File,
                                               ui->chosenReadIC4File};

    // Prepare to read the files; when the read is complete we will save
    // the files as necessary
    if (readBuffer)
    {
        delete readBuffer;
    }
    readBuffer = new QBuffer();

    // Try to open each file to make sure it's writable first. Then close it.
    bool hadError = false;
    for (size_t x = 0; x < sizeof(readBoxes)/sizeof(readBoxes[0]); x++)
    {
        if (readBoxes[x]->isChecked())
        {
            QFile tmp(readChosenFileEdits[x]->text());
            if (!tmp.open(QFile::ReadWrite))
            {
                hadError = true;
            }
            else
            {
                tmp.close();
            }
        }
    }

    // If there was an error creating one of the files, bail out.
    if (hadError)
    {
        programmerReadStatusChanged(ReadError);
        return;
    }

    // Open up the buffer to read into
    readBuffer->open(QFile::ReadWrite);

    // Now start reading it!
    resetAndShowStatusPage();
    p->readSIMM(readBuffer);
}

void MainWindow::finishMultiRead()
{
    QCheckBox * const readBoxes[] = {ui->readIC1CheckBox,
                                     ui->readIC2CheckBox,
                                     ui->readIC3CheckBox,
                                     ui->readIC4CheckBox};

    QLineEdit * const readChosenFileEdits[] = {ui->chosenReadIC1File,
                                               ui->chosenReadIC2File,
                                               ui->chosenReadIC3File,
                                               ui->chosenReadIC4File};

    bool hadError = false;

    QFile *files[sizeof(readBoxes)/sizeof(readBoxes[0])] = {NULL, NULL, NULL, NULL};
    for (size_t x = 0; x < sizeof(files)/sizeof(files[0]); x++)
    {
        if (readBoxes[x]->isChecked())
        {
            files[x] = new QFile(readChosenFileEdits[x]->text());
            if (!files[x]->open(QFile::WriteOnly))
            {
                hadError = true;
            }
        }
    }

    if (hadError)
    {
        for (size_t x = 0; x < sizeof(files)/sizeof(files[0]); x++)
        {
            if (files[x]) delete files[x];
        }

        programmerReadStatusChanged(ReadError);
        return;
    }

    // Take the final read file and de-interleave it into separate chip files
    readBuffer->seek(0);
    while (!readBuffer->atEnd())
    {
        // Go in reverse order through the files so the data is saved to the
        // files in the correct order
        for (int y = (int)(sizeof(files)/sizeof(files[0])) - 1; y >= 0; y--)
        {
            char c = 0xFF;
            if (readBuffer->getChar(&c)) // grab a character...
            {
                // and as long as it was a success, stick it into the appropriate file
                // (IF we have one), or discard it if we didn't care about the chip
                // it went with.
                if (files[y])
                {
                    files[y]->putChar(c);
                }
            }
            else // no success? we're probably near end of SIMM. just discard it and close the file
            {
                files[y]->close();
                delete files[y];
                files[y] = NULL;
            }
        }
    }

    // Close the individual files that are remaining
    for (size_t x = 0; x < sizeof(files)/sizeof(files[0]); x++)
    {
        if (files[x])
        {
            files[x]->close();
            delete files[x];
        }
    }
}

void MainWindow::returnToControlPage()
{
    // Depending on what we were doing, return to the correct page
    if (writeBuffer || readBuffer)
    {
        ui->pages->setCurrentWidget(ui->flashChipsPage);
    }
    else
    {
        ui->pages->setCurrentWidget(ui->controlPage);
    }
}
