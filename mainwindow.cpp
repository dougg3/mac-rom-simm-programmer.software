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

    // Decide whether or not to verify after write
    ui->verifyAfterWriteBox->setChecked(settings.value(verifyAfterWriteKey, true).toBool());

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
    verifyArray = NULL;
    verifyBuffer = NULL;

    connect(p, SIGNAL(writeStatusChanged(WriteStatus)), SLOT(programmerWriteStatusChanged(WriteStatus)));
    connect(p, SIGNAL(writeTotalLengthChanged(uint32_t)), SLOT(programmerWriteTotalLengthChanged(uint32_t)));
    connect(p, SIGNAL(writeCompletionLengthChanged(uint32_t)), SLOT(programmerWriteCompletionLengthChanged(uint32_t)));
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
    // We are not doing a verify after a write..
    readVerifying = false;
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
        p->writeToSIMM(writeFile);
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
    if (fi.exists() && fi.isFile())
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

void MainWindow::programmerWriteStatusChanged(WriteStatus newStatus)
{
    switch (newStatus)
    {
    case WriteErasing:
        ui->statusLabel->setText("Erasing SIMM (this may take a few seconds)...");
        break;
    case WriteComplete:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        if (ui->verifyAfterWriteBox->isChecked())
        {
            // Set up the read buffers
            readVerifying = true;
            if (verifyArray) delete verifyArray;
            verifyArray = new QByteArray((int)p->SIMMCapacity(), 0);
            if (verifyBuffer) delete verifyBuffer;
            verifyBuffer = new QBuffer(verifyArray);
            if (!verifyBuffer->open(QBuffer::ReadWrite))
            {
                delete verifyBuffer;
                delete verifyArray;
                verifyBuffer = NULL;
                verifyArray = NULL;
                programmerReadStatusChanged(ReadError);
                return;
            }

            // Start reading from SIMM to temporary RAM buffer
            resetAndShowStatusPage();

            // Only read back the size of the file if we can. This will save
            // some time and prevent us from needing to read the ENTIRE SIMM
            // if the file is only a quarter of the size of the SIMM.
            uint32_t readLen = 0;
            QFile temp(ui->chosenWriteFile->text());
            if (temp.exists())
            {
                qint64 tmpLen = temp.size();
                if (tmpLen > 0)
                {
                    readLen = static_cast<uint32_t>(tmpLen);
                }
                else
                {
                    readLen = 0;
                }
            }

            p->readSIMM(verifyBuffer, readLen);
            qDebug() << "Reading from SIMM for verification...";
        }
        else
        {
            QMessageBox::information(this, "Write complete", "The write operation finished.");
            ui->pages->setCurrentWidget(ui->controlPage);
        }
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
    case WriteNeedsFirmwareUpdate:
        if (writeFile)
        {
            writeFile->close();
            delete writeFile;
            writeFile = NULL;
        }

        ui->pages->setCurrentWidget(ui->controlPage);
        QMessageBox::warning(this, "Firmware update needed", "The programmer board needs a firmware update to support a larger SIMM. Please update the firmware and try again.");
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
        if (readVerifying)
        {
            ui->statusLabel->setText("Verifying SIMM contents...");
        }
        else
        {
            ui->statusLabel->setText("Reading SIMM contents...");
        }
        break;
    case ReadComplete:
        if (readVerifying)
        {
            // Re-open the write file temporarily to compare it against the buffer.
            QFile temp(ui->chosenWriteFile->text());
            if (!temp.open(QFile::ReadOnly))
            {
                QMessageBox::warning(this, "Verify failed", "Unable to open file for verification.");
            }
            else
            {
                QByteArray fileBytes = temp.readAll();

                if (fileBytes.count() <= verifyArray->count())
                {
                    const char *fileBytesPtr = fileBytes.constData();
                    const char *readBytesPtr = verifyArray->constData();

                    if (memcmp(fileBytesPtr, readBytesPtr, fileBytes.count()) != 0)
                    {
                        // Now let's do some trickery and figure out which chip is acting up (or chips)
                        uint8_t badICMask = 0;

                        // Keep a list of which chips are reading bad data back
                        for (int x = 0; (x < fileBytes.count()) && (badICMask != 0xF); x++)
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
                                badICMask |= (1 << (3 - (x % 4)));
                            }
                        }

                        // Make a comma-separated list of IC names from this list
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

                        QMessageBox::warning(this, "Verify error", "The data read back from the SIMM did not match the data written to it. Bad data on chips: " + icList);
                    }
                    else
                    {
                        QMessageBox::information(this, "Write complete", "The write operation finished, and the contents were verified successfully.");
                    }
                }
                else
                {
                    QMessageBox::warning(this, "Verify error", "The data read back from the SIMM did not match the data written to it--wrong amount of data read back.");
                }
            }

            // Close stuff not needed anymore
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
                verifyBuffer = NULL;
            }

            if (verifyArray)
            {
                delete verifyArray;
                verifyArray = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::information(this, "Read complete", "The read operation finished.");
        }
        break;
    case ReadError:
        if (readVerifying)
        {
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
            }
            verifyBuffer = NULL;
            if (verifyArray) delete verifyArray;
            verifyArray = NULL;

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Verify error", "An error occurred reading the SIMM contents for verification.");
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Read error", "An error occurred reading from the SIMM.");
        }
        break;
    case ReadCancelled:
        if (readVerifying)
        {
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
            }
            verifyBuffer = NULL;
            if (verifyArray) delete verifyArray;
            verifyArray = NULL;

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Verify cancelled", "The verify operation was cancelled.");
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Read cancelled", "The read operation was cancelled.");
        }
        break;
    case ReadTimedOut:
        if (readVerifying)
        {
            if (verifyBuffer)
            {
                verifyBuffer->close();
                delete verifyBuffer;
            }
            verifyBuffer = NULL;
            if (verifyArray) delete verifyArray;
            verifyArray = NULL;

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Verify timed out", "The verify operation timed out.");
        }
        else
        {
            if (readFile)
            {
                readFile->close();
                delete readFile;
                readFile = NULL;
            }

            ui->pages->setCurrentWidget(ui->controlPage);
            QMessageBox::warning(this, "Read timed out", "The read operation timed out.");
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
    uint32_t newCapacity = (uint32_t)ui->simmCapacityBox->itemData(index).toUInt();
    p->setSIMMCapacity(newCapacity);
    QSettings settings;
    if (!initializing)
    {
        // If we're not initializing (it gets called while we're initializing),
        // go ahead and save this as the new default.
        settings.setValue(selectedCapacityKey, newCapacity);
    }
}

void MainWindow::on_verifyAfterWriteBox_toggled(bool checked)
{
    // Save this as the new default.
    QSettings settings;
    settings.setValue(verifyAfterWriteKey, checked);
}

void MainWindow::on_actionAbout_SIMM_Programmer_triggered()
{
    AboutBox::instance()->show();
}
