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
    ui(new Ui::MainWindow)
{
    initializing = true;
    // Make default QSettings use these settings
    QCoreApplication::setOrganizationName("Doug Brown");
    QCoreApplication::setOrganizationDomain("downtowndougbrown.com");
    QCoreApplication::setApplicationName("SIMMProgrammer");
    QSettings settings;

    p = new Programmer();
    ui->setupUi(this);
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

    readFile = NULL;
    writeFile = NULL;

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
        ui->statusLabel->setText("Erasing SIMM (this may take a few seconds)...");
        break;
    case WriteCompleteNoVerify:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        QMessageBox::information(this, "Write complete", "The write operation finished.");
        ui->pages->setCurrentWidget(ui->controlPage);
        break;
    case WriteCompleteVerifyOK:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        QMessageBox::information(this, "Write complete", "The write operation finished, and the contents were verified successfully.");
        ui->pages->setCurrentWidget(ui->controlPage);
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
        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Verify error", "An error occurred reading the SIMM contents for verification.");
        break;
    case WriteVerifyCancelled:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Verify cancelled", "The verify operation was cancelled.");
        break;
    case WriteVerifyTimedOut:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Verify timed out", "The verify operation timed out.");
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
        break;
    case WriteError:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write error", "An error occurred writing to the SIMM.");
        break;
    case WriteNeedsFirmwareUpdateBiggerSIMM:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support a larger SIMM. Please update the firmware and try again.");
        break;
    case WriteNeedsFirmwareUpdateVerifyWhileWrite:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support the \"verify while writing\" capability. Please update the firmware and try again.");
        break;
    case WriteCancelled:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write cancelled", "The write operation was cancelled.");
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

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write error", "An error occurred erasing the SIMM.");
        break;
    case WriteTimedOut:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Write timed out", "The write operation timed out.");
        break;
    case WriteFileTooBig:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "File too big", "The file you chose to write to the SIMM is too big according to the chip size you have selected.");
        break;
    case WriteEraseBlockWrongSize:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Bad erase size", "The programmer cannot handle the erase size you chose.");
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

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::information(this, "Read complete", "The read operation finished.");
        break;
    case ReadError:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Read error", "An error occurred reading from the SIMM.");
        break;
    case ReadCancelled:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Read cancelled", "The read operation was cancelled.");
        break;
    case ReadTimedOut:
        if (readFile)
        {
            readFile->close();
            delete readFile;
            readFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Read timed out", "The read operation timed out.");
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
    ui->pages->setCurrentWidget(ui->controlPage);
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

    ui->pages->setCurrentWidget(ui->controlPage);
    QMessageBox::warning(this, "Verify error", "The data read back from the SIMM did not match the data written to it. Bad data on chips: " + icList);
}
