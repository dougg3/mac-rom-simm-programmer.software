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
#include "fc8compressor.h"
#include "createblankdiskdialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QSettings>
#include <QBuffer>
#include <QThread>
#include <algorithm>
#include <QLocale>
#include <QCryptographicHash>

static Programmer *p;

#define selectedCapacityKey     "selectedCapacity"
#define verifyAfterWriteKey     "verifyAfterWrite"
#define verifyWhileWritingKey   "verifyWhileWriting"
#define selectedEraseSizeKey    "selectedEraseSize"
#define extendedViewKey         "extendedView"

struct SIMMDesc {
    uint32_t saveValue;
    const char *text;
    uint32_t size;
    uint32_t chipType;
};

SIMMDesc simmTable[] ={
    {0, "128KB (4x 256Kb PLCC)", 128, SIMM_PLCC_x8 },
    {1, "256KB (4x 512Kb PLCC)", 256, SIMM_PLCC_x8 },
    {2, "512KB (4x 1Mb PLCC)",   512, SIMM_PLCC_x8 },
    {3, "1MB (4x 2Mb PLCC)",    1024, SIMM_PLCC_x8 },
    {4, "2MB (4x 4Mb PLCC)",    2048, SIMM_PLCC_x8 },
    {8, "2MB (2x 8Mb TSOP)",    2048, SIMM_TSOP_x16},
    {9, "4MB (4x 8Mb TSOP)",    4096, SIMM_TSOP_x8 },
    {5, "4MB (2x 16Mb TSOP)",   4096, SIMM_TSOP_x16},
    {6, "8MB (4x 16Mb TSOP)",   8192, SIMM_TSOP_x8 },
    {7, "8MB (2x 32Mb TSOP)",   8192, SIMM_TSOP_x16},
};

static const struct
{
    uint32_t checksum;
    const char *model;
} romChecksumsAndModels[] = {
    {0x28BA61CEUL, "128k or 512k"},
    {0x28BA4E50UL, "128k or 512k"},
    {0x4D1EEEE1UL, "Plus"},
    {0x4D1EEAE1UL, "Plus"},
    {0x4D1F8172UL, "Plus"},
    {0xB2E362A8UL, "SE"},
    {0xB306E171UL, "SE FDHD"},
    {0x9779D2C4UL, "II"},
    {0x97851DB6UL, "II"},
    {0x97221136UL, "IIx, IIcx, or SE/30"},
    {0x368CADFEUL, "IIci"},
    {0x36B7FB6CUL, "IIsi"},
    {0x4147DD77UL, "IIfx"},
    {0x4957EB49UL, "IIvx or IIvi"},
    {0x49579803UL, "IIvx or IIvi"},
    {0xA49F9914UL, "Classic"},
    {0x3193670EUL, "Classic II"},
    {0xECD99DC0UL, "Color Classic"},
    {0xEDE66CBDUL, "Color Classic II, LC 550, or TV"},
    {0xEAF1678DUL, "Color Classic II, LC 550, or TV"},
    {0x350EACF0UL, "LC"},
    {0x35C28F5FUL, "LC II"},
    {0xEC904829UL, "LC III"},
    {0xECBBC41CUL, "LC III"},
    {0x064DC91DUL, "LC 580"},
    {0xF1A6F343UL, "Quadra/Centris 610, 650, or 800"},
    {0x420DBFF3UL, "Quadra 700 or 900"},
    {0x3DC27823UL, "Quadra 950"},
    {0xFF7439EEUL, "Quadra 605, LC 475, or LC 575"},
    {0x06684214UL, "Quadra 630"},
    {0x5BF10FD1UL, "Quadra 660av or 840av"},
    {0x87D3C814UL, "Quadra 660av or 840av"},
};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    writeFile(NULL),
    readFile(NULL),
    writeBuffer(NULL),
    readBuffer(NULL),
    activeMessageBox(NULL),
    chipID(":/chipid/chipid.txt")
{
    initializing = true;
    // Make default QSettings use these settings
    QCoreApplication::setOrganizationName("Doug Brown");
    QCoreApplication::setOrganizationDomain("downtowndougbrown.com");
    QCoreApplication::setApplicationName("SIMMProgrammer");
    QSettings settings;

    p = new Programmer();
    ui->setupUi(this);

    // On Mac and Linux, make it a little wider due to larger font
#if defined(Q_OS_MACX) || defined(Q_OS_LINUX)
    resize(width() + 200, height());
#endif

    if (settings.value(extendedViewKey, false).toBool())
    {
        setUseExtendedUI(true);
        ui->actionExtended_UI->setChecked(true);
    }

    hideFlashIndividualControls();
    ui->pages->setCurrentWidget(ui->notConnectedPage);
    ui->tabWidget->setCurrentWidget(ui->writeTab);
    ui->actionUpdate_firmware->setEnabled(false);

    // Fill in the list of SIMM chip capacities (programmer can support anywhere up to 8 MB of space)
    for (size_t i = 0; i < sizeof(simmTable)/sizeof(simmTable[0]); i++)
    {
        ui->simmCapacityBox->addItem(simmTable[i].text, QVariant(simmTable[i].saveValue));
    }

    // Select 2 MB by default (it's what most people will want), or load last-used setting
    QVariant selectedCapacity = settings.value(selectedCapacityKey, QVariant(4));
    int selectedIndex = ui->simmCapacityBox->findData(selectedCapacity);
    if (selectedIndex != -1)
    {
        ui->simmCapacityBox->setCurrentIndex(selectedIndex);
    }

    // Fill in the list of verification options
    QList<QComboBox *> verifyBoxes;
    verifyBoxes << ui->verifyBox;
    verifyBoxes << ui->createVerifyBox;
    foreach (QComboBox *verifyBox, verifyBoxes)
    {
        verifyBox->addItem("Don't verify", QVariant(NoVerification));
        verifyBox->addItem("Verify while writing", QVariant(VerifyWhileWriting));
        verifyBox->addItem("Verify after writing", QVariant(VerifyAfterWrite));
    }

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
        foreach (QComboBox *verifyBox, verifyBoxes)
        {
            verifyBox->setCurrentIndex(selectedIndex);
        }
    }

    // Fill in list of "write first xxx bytes" options
    QList<QComboBox *> howMuchToWriteBoxes;
    howMuchToWriteBoxes << ui->howMuchToWriteBox;
    howMuchToWriteBoxes << ui->createHowMuchToWriteBox;
    foreach (QComboBox *howMuchToWriteBox, howMuchToWriteBoxes)
    {
        howMuchToWriteBox->addItem("Erase/write entire SIMM", QVariant(0));
        howMuchToWriteBox->addItem("Only erase/write first 256 KB", QVariant(256*1024));
        howMuchToWriteBox->addItem("Only erase/write first 512 KB", QVariant(512*1024));
        howMuchToWriteBox->addItem("Only erase/write first 1 MB", QVariant(1024*1024));
        howMuchToWriteBox->addItem("Only erase/write first 1.5 MB", QVariant(3*512*1024));
        howMuchToWriteBox->addItem("Only erase/write first 2 MB", QVariant(2*1024*1024));
        howMuchToWriteBox->addItem("Only erase/write first 4 MB", QVariant(4*1024*1024));
        howMuchToWriteBox->addItem("Only erase/write first 8 MB", QVariant(8*1024*1024));
    }

    // Select "erase entire SIMM" by default, or load last-used setting
    QVariant selectedEraseSize = settings.value(selectedEraseSizeKey, QVariant(0));
    selectedIndex = ui->howMuchToWriteBox->findData(selectedEraseSize);
    if (selectedIndex != -1)
    {
        foreach (QComboBox *howMuchToWriteBox, howMuchToWriteBoxes)
        {
            howMuchToWriteBox->setCurrentIndex(selectedIndex);
        }
    }

    ui->chosenWriteFile->setText("");
    ui->chosenReadFile->setText("");
    ui->writeToSIMMButton->setEnabled(false);
    ui->readFromSIMMButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->statusLabel->setText("");
    ui->cancelButton->setEnabled(false);

    ui->writeCombinedFileToSIMMButton->setEnabled(false);
    ui->saveCombinedFileButton->setEnabled(false);
    ui->createROMErrorText->setText("");
    // Allow dropping a ROM and disk image simultaneously
    ui->createROMGroupBox->setMaxFiles(2);

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
        ui->chosenWriteFile->setText(filename);
        ui->writeToSIMMButton->setEnabled(true);
    }
}

void MainWindow::on_selectReadFileButton_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this, "Save ROM image as:");
    if (!filename.isNull())
    {
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
    doInternalWrite(new QFile(ui->chosenWriteFile->text()));
}

void MainWindow::doInternalWrite(QIODevice *device)
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
    writeFile = device;
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
            p->writeToSIMM(writeFile, 0, qMin(howMuchToErase, p->SIMMCapacity()));
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
    }
    else
    {
        ui->writeToSIMMButton->setEnabled(false);
    }
}

void MainWindow::on_chosenReadFile_textEdited(const QString &newText)
{
    QFileInfo fi(newText);
    if (!newText.isEmpty() && fi.dir().exists())
    {
        ui->readFromSIMMButton->setEnabled(true);
    }
    else
    {
        ui->readFromSIMMButton->setEnabled(false);
    }
}

void MainWindow::on_writeGroupBox_fileDropped(const QString &filePath)
{
    ui->chosenWriteFile->setText(filePath);
    on_chosenWriteFile_textEdited(filePath);
}

void MainWindow::on_readGroupBox_fileDropped(const QString &filePath)
{
    ui->chosenReadFile->setText(filePath);
    on_chosenReadFile_textEdited(filePath);
}

void MainWindow::on_createROMGroupBox_fileDropped(const QString &filePath)
{
    // It could be a base ROM or a disk image.
    QFile droppedFile(filePath);
    if (!droppedFile.open(QFile::ReadOnly))
    {
        return;
    }

    // Let's see what type of file it is.
    QByteArray data = droppedFile.read(0x1708);
    droppedFile.close();

    if (data.length() != 0x1708)
    {
        // It's too short to be anything! Bail!
        return;
    }

    // Is it a ROM? Do a very simplified check similar to checkBaseROMValidity()
    if ((data.at(0x1706) == 0x4E && data.at(0x1707) == 0x75) ||
        (data.at(0x1706) == 0x67))
    {
        ui->chosenBaseROMFile->setText(filePath);
        on_chosenBaseROMFile_textEdited(filePath);
    }
    // Not a ROM; it might be a disk image. Look for compressed disk image header
    // or HFS to get an idea about what's going on.
    else if (data.startsWith(QByteArray("FC8", 3)) ||
             (data.at(1024) == 'B' && data.at(1025) == 'D'))
    {
        ui->chosenDiskImageFile->setText(filePath);
        on_chosenDiskImageFile_textEdited(filePath);
    }
    // Let's just silently ignore the dropped if it's not one of these.
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

        returnToControlPage();
        showMessageBox(QMessageBox::Information, "Write complete", "The write operation finished.");
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

        returnToControlPage();
        showMessageBox(QMessageBox::Information, "Write complete", "The write operation finished, and the contents were verified successfully.");
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
        showMessageBox(QMessageBox::Warning, "Verify error", "An error occurred reading the SIMM contents for verification.");
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
        showMessageBox(QMessageBox::Warning, "Verify cancelled", "The verify operation was cancelled.");
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
        showMessageBox(QMessageBox::Warning, "Verify timed out", "The verify operation timed out.");
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
        showMessageBox(QMessageBox::Warning, "Write error", "An error occurred writing to the SIMM.");
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
        showMessageBox(QMessageBox::Warning, "Firmware update needed", "The programmer board needs a firmware update to support a larger SIMM. Please update the firmware and try again.");
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
        showMessageBox(QMessageBox::Warning, "Firmware update needed", "The programmer board needs a firmware update to support the \"verify while writing\" capability. Please update the firmware and try again.");
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
        showMessageBox(QMessageBox::Warning, "Firmware update needed", "The programmer board needs a firmware update to support the \"erase only a portion of the SIMM\" capability. Please update the firmware and try again.");
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
        showMessageBox(QMessageBox::Warning, "Firmware update needed", "The programmer board needs a firmware update to support the \"program individual chips\" capability. Please update the firmware and try again.");
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
        showMessageBox(QMessageBox::Warning, "Write cancelled", "The write operation was cancelled.");
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
        showMessageBox(QMessageBox::Warning, "Write error", "An error occurred erasing the SIMM.");
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
        showMessageBox(QMessageBox::Warning, "Write timed out", "The write operation timed out.");
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
        showMessageBox(QMessageBox::Warning, "File too big", "The file you chose to write to the SIMM is too big according to the chip size you have selected.");
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
        showMessageBox(QMessageBox::Warning, "Bad erase size", "The programmer cannot handle the erase size you chose.");
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
        showMessageBox(QMessageBox::Information, "Test passed", "The electrical test passed successfully.");
        break;
    case ElectricalTestFailed:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Test failed", "The electrical test failed:\n\n" + electricalTestString);
        break;
    case ElectricalTestTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Test timed out", "The electrical test operation timed out.");
        break;
    case ElectricalTestCouldntStart:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Communication error", "Unable to communicate with programmer board.");
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
        showMessageBox(QMessageBox::Information, "Read complete", "The read operation finished.");
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
        showMessageBox(QMessageBox::Warning, "Read error", "An error occurred reading from the SIMM.");
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
        showMessageBox(QMessageBox::Warning, "Read cancelled", "The read operation was cancelled.");
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
        showMessageBox(QMessageBox::Warning, "Read timed out", "The read operation timed out.");
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

        // Get the straight and shifted unlock results from the identification command
        QList<uint8_t> manufacturersStraight;
        QList<uint8_t> devicesStraight;
        QList<uint8_t> manufacturersShifted;
        QList<uint8_t> devicesShifted;
        for (int i = 0; i < 4; i++)
        {
            uint8_t m, d;
            p->getChipIdentity(i, &m, &d, false);
            manufacturersStraight << m;
            devicesStraight << d;
            p->getChipIdentity(i, &m, &d, true);
            manufacturersShifted << m;
            devicesShifted << d;
        }

        // Pass it all to ChipID to see what it finds
        QList<ChipID::ChipInfo> chipInfo;
        if (chipID.findChips(manufacturersStraight, devicesStraight, manufacturersShifted, devicesShifted, chipInfo) && !chipInfo.isEmpty())
        {
            // It found something! Now, try to make some sense of the results.
            QSet<uint32_t> capacities;
            int chipNumber = 1;

            // If we get here, update the message to say how many chips we found
            identifyString = QString("The %1 chips identified themselves as:").arg(chipInfo.count());

            foreach (ChipID::ChipInfo const &ci, chipInfo)
            {
                QString thisString;
                         thisString = QString("\nIC%1: %2 %3 [0x%4, 0x%5]").arg(chipNumber++)
                        .arg(ci.manufacturer)
                        .arg(ci.product)
                        .arg(QString::number(ci.manufacturerID, 16).toUpper(), 2, QChar('0'))
                        .arg(QString::number(ci.productID, 16).toUpper(), 2, QChar('0'));
                identifyString.append(thisString);

                capacities << ci.capacity;
            }

            uint32_t capacityToDisplay = 0;

            // Check for problems in the returned data. Like maybe only one of the chips has a shorted data pin.
            if (capacities.count() > 1 || capacities.contains(0))
            {
                if (capacities.contains(0))
                {
                    identifyString.append("\n\nThere seems to be something wrong with one or more of the chips on this SIMM.");
                }
                else
                {
                    identifyString.append("\n\nThere seems to be a mismatch of chip types on this SIMM.");
                }
                capacities.remove(0);
                QList<uint32_t> allValues = capacities.values();
                // Sort the list so that the lowest capacity is the one we display. If there is a mismatch,
                // the mismatch message will have been printed above
                std::sort(allValues.begin(), allValues.end());
                if (allValues.count() > 0)
                {
                    capacityToDisplay = allValues[0] * chipInfo.count();
                }
            }
            else
            {
                // If we get here, the chips are all the same capacity and all recognized.
                capacityToDisplay = capacities.values().at(0) * chipInfo.count();
            }

            if (capacityToDisplay != 0)
            {
                identifyString.append(QString("\n\nTotal Capacity = %1").arg(displayableFileSize(capacityToDisplay)));
            }

            // Find the first chip matching the capacity we determined earlier
            foreach (ChipID::ChipInfo const &ci, chipInfo)
            {
                if (ci.capacity > 0 && ci.capacity == capacityToDisplay / chipInfo.count())
                {
                    // We found one. Let's figure out what item we should choose in the list.
                    const uint32_t chipCapacityBits = ci.capacity * 8;
                    const bool tsop = ci.width == 16 || ci.unlockShifted;
                    const int chipCount = chipInfo.count();
                    QString displayChipCapacity;
                    if (chipCapacityBits >= 1048576)
                    {
                        displayChipCapacity = QString("%1Mb").arg(chipCapacityBits / 1048576);
                    }
                    else if (chipCapacityBits >= 1024)
                    {
                        displayChipCapacity = QString("%1Kb").arg(chipCapacityBits / 1024);
                    }
                    else
                    {
                        displayChipCapacity = "Unknown";
                    }
                    QString chipConfigDetail;
                    chipConfigDetail = QString("(%1x %2 %3)").arg(chipCount).arg(displayChipCapacity).arg(tsop ? "TSOP" : "PLCC");
                    identifyString.append(" ");
                    identifyString.append(chipConfigDetail);

                    // Special cases: select the 8 MB option for SIMMs larger than 8 MB. The programmer only has enough address
                    // lines to access 8 MB of data at a time, but there are SIMMs out there that are larger.
                    if (chipCount == 4 && chipCapacityBits > 16*1048576)
                    {
                        chipConfigDetail = "(4x 16Mb TSOP)";
                    }
                    else if (chipCount == 2 && chipCapacityBits > 32*1048576)
                    {
                        chipConfigDetail = "(2x 32Mb TSOP)";
                    }

                    // Find a matching item in the dropdown
                    bool foundMatch = false;
                    for (size_t i = 0; i < sizeof(simmTable)/sizeof(simmTable[0]); i++)
                    {
                        QString displayName(simmTable[i].text);
                        if (displayName.contains(chipConfigDetail))
                        {
                            foundMatch = true;
                            ui->simmCapacityBox->setCurrentIndex(i);
                            break;
                        }
                    }

                    // If we don't find a match, add a note to the bottom of the chip ID
                    if (!foundMatch)
                    {
                        identifyString.append("\n\nThis chip combination is not currently supported, but you can try selecting a similar combination with a different capacity.");
                    }

                    break;
                }
            }
        }
        else
        {
            // If we failed to identify the chips, then there might be a problem with the programmable ROM SIMM,
            // or maybe it's not a programmable SIMM, in which case we have just read back the first 12 bytes
            // of the SIMM (first 4 bytes = manufacturers straight, next 4 bytes = devices straight/manufacturers shifted,
            // next 4 bytes = devices shifted). See if it has a standard Mac ROM checksum. It might be a stock ROM SIMM.
            QString macModel;

            // Quick sanity check: the shifted identification addresses are offset by 1 byte.
            // So if the straight device response is the same as the shifted manufacturer response,
            // it likely means we aren't unlocking any flash chips. It may very well be a mask ROM.
            if (devicesStraight == manufacturersShifted)
            {
                uint32_t possibleRomChecksum = manufacturersStraight[0] |
                        (static_cast<uint32_t>(manufacturersStraight[1]) << 8) |
                        (static_cast<uint32_t>(manufacturersStraight[2]) << 16) |
                        (static_cast<uint32_t>(manufacturersStraight[3]) << 24);

                // See if we find a matching standard Mac ROM checksum in our internal database
                for (size_t i = 0; i < sizeof(romChecksumsAndModels) / sizeof(romChecksumsAndModels[0]); i++)
                {
                    if (romChecksumsAndModels[i].checksum == possibleRomChecksum)
                    {
                        macModel = romChecksumsAndModels[i].model;
                        break;
                    }
                }
            }

            if (!macModel.isEmpty())
            {
                identifyString = QString("This appears to be a standard Macintosh %1 ROM SIMM according to its checksum.").arg(macModel);
                identifyString += "\n\nIt is likely composed of mask ROMs and not programmable. If this is a programmable SIMM, there might be a problem with it.";
            }
            else
            {
                // If we STILL failed to identify anything, fall back to the old method where we just dump the raw data
                // based on the current selected chip type
                if (p->SIMMChip() == SIMM_TSOP_x16)
                {
                    for (int x = 0; x < 2; x++)
                    {
                        QString thisString;
                        uint8_t manufacturer0 = 0, manufacturer1 = 0;
                        uint8_t device0 = 0, device1 = 0;
                        p->getChipIdentity(x*2, &manufacturer0, &device0, p->selectedSIMMTypeUsesShiftedUnlock());
                        p->getChipIdentity(x*2+1, &manufacturer1, &device1, p->selectedSIMMTypeUsesShiftedUnlock());
                        thisString = QString("\nIC%1: Manufacturer 0x%2, Device 0x%3").arg(x + 1)
                                .arg(QString::number((static_cast<uint16_t>(manufacturer1) << 8) | manufacturer0, 16).toUpper(), 4, QChar('0'))
                                .arg(QString::number((static_cast<uint16_t>(device1) << 8) | device0, 16).toUpper(), 4, QChar('0'));
                        identifyString.append(thisString);
                    }
                }
                else
                {
                    for (int x = 0; x < 4; x++)
                    {
                        QString thisString;
                        uint8_t manufacturer = 0;
                        uint8_t device = 0;
                        p->getChipIdentity(x, &manufacturer, &device, p->selectedSIMMTypeUsesShiftedUnlock());
                        thisString = QString("\nIC%1: Manufacturer 0x%2, Device 0x%3").arg(x + 1)
                                .arg(QString::number(manufacturer, 16).toUpper(), 2, QChar('0'))
                                .arg(QString::number(device, 16).toUpper(), 2, QChar('0'));
                        identifyString.append(thisString);
                    }
                }
            }
        }

        showMessageBox(QMessageBox::Information, "Identification complete", identifyString);
        break;
    }
    case IdentificationError:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Identification error", "An error occurred identifying the chips on the SIMM.");
        break;
    case IdentificationTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Identification timed out", "The identification operation timed out.");
        break;
    case IdentificationNeedsFirmwareUpdate:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Firmware update needed", "The programmer board needs a firmware update to support a larger SIMM. Please update the firmware and try again.");
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
        showMessageBox(QMessageBox::Information, "Firmware update complete", "The firmware update operation finished.");
        break;
    case FirmwareFlashError:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Firmware update error", "An error occurred writing firmware to the device.");
        break;
    case FirmwareFlashCancelled:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Firmware update cancelled", "The firmware update was cancelled.");
        break;
    case FirmwareFlashTimedOut:
        ui->pages->setCurrentWidget(ui->controlPage);
        showMessageBox(QMessageBox::Warning, "Firmware update timed out", "The firmware update operation timed out.");
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
        QString compatibilityError;
        if (firmwareIsCompatible(filename, compatibilityError))
        {
            resetAndShowStatusPage();
            p->flashFirmware(filename);
            qDebug() << "Updating firmware...";
        }
        else
        {
            showMessageBox(QMessageBox::Warning, "Invalid firmware file", compatibilityError);
        }
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
    // If a message box currently visible, dismiss it
    if (activeMessageBox)
    {
        activeMessageBox->close();
        messageBoxFinished();
    }
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
    showMessageBox(QMessageBox::Warning, "Programmer lost connection", "Lost contact with the programmer board. Unplug it, plug it back in, and try again.");
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
    p->setSIMMType(simmTable[index].size*1024, simmTable[index].chipType);
    QSettings settings;
    if (!initializing)
    {
        // If we're not initializing (it gets called while we're initializing),
        // go ahead and save this as the new default.
        uint32_t saveValue = static_cast<uint32_t>(ui->simmCapacityBox->itemData(index).toUInt());
        settings.setValue(selectedCapacityKey, saveValue);

        // This can affect the error status of the ROM creation section
        updateCreateROMControlStatus();
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

        // Update the other combo box without allowing it to emit a signal
        ui->createVerifyBox->blockSignals(true);
        ui->createVerifyBox->setCurrentIndex(index);
        ui->createVerifyBox->blockSignals(false);
    }
}

void MainWindow::on_createVerifyBox_currentIndexChanged(int index)
{
    // Update the index on the main control, which will actually apply
    // the change to the setting.
    if (!initializing)
    {
        ui->verifyBox->setCurrentIndex(index);
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

        // Update the other combo box without allowing it to emit a signal
        ui->createHowMuchToWriteBox->blockSignals(true);
        ui->createHowMuchToWriteBox->setCurrentIndex(index);
        ui->createHowMuchToWriteBox->blockSignals(false);
    }
}

void MainWindow::on_createHowMuchToWriteBox_currentIndexChanged(int index)
{
    // Update the index on the main control, which will actually apply
    // the change to the setting.
    if (!initializing)
    {
        ui->howMuchToWriteBox->setCurrentIndex(index);
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
    showMessageBox(QMessageBox::Warning, "Verify error", "The data read back from the SIMM did not match the data written to it. Bad data on chips: " + icList);
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

void MainWindow::on_selectBaseROMButton_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a base ROM image:");
    if (!filename.isNull())
    {
        ui->chosenBaseROMFile->setText(filename);
        updateCreateROMControlStatus();
    }
}


void MainWindow::on_selectDiskImageButton_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a disk image to add to the ROM:");\
    if (!filename.isNull())
    {
        ui->chosenDiskImageFile->setText(filename);
        updateCreateROMControlStatus();
    }
}


void MainWindow::on_chosenBaseROMFile_textEdited(const QString &text)
{
    Q_UNUSED(text);
    updateCreateROMControlStatus();
}


void MainWindow::on_chosenDiskImageFile_textEdited(const QString &text)
{
    Q_UNUSED(text);
    updateCreateROMControlStatus();
}

void MainWindow::updateCreateROMControlStatus()
{
    QString baseROMError;
    QString diskImageError;
    bool baseROMValid = checkBaseROMValidity(baseROMError);
    bool alreadyCompressed = false;
    bool diskImageValid = checkDiskImageValidity(diskImageError, alreadyCompressed);
    bool error = true;

    ui->writeCombinedFileToSIMMButton->setEnabled(baseROMValid && diskImageValid);
    ui->saveCombinedFileButton->setEnabled(baseROMValid && diskImageValid);

    if (!baseROMValid && !baseROMError.isEmpty())
    {
        ui->createROMErrorText->setText(baseROMError);
    }
    else if (!diskImageValid && !diskImageError.isEmpty())
    {
        ui->createROMErrorText->setText(diskImageError);
    }
    else if (baseROMValid && diskImageValid)
    {
        QByteArray uncompressedImage = uncompressedDiskImage();
        bool supportsCompression = checkBaseROMCompressionSupport();
        bool shouldCompress = supportsCompression && !alreadyCompressed;
        error = false;
        if (shouldCompress &&
            !FC8Compressor::hashMatchesFile(compressedImageFileHash, uncompressedImage))
        {
            ui->createROMErrorText->setText("Compressing...");

            // Run the compression in the background. When it completes, this will re-run.
            compressImageInBackground(uncompressedImage, false);

            // While it's compressing, we can't allow writing/saving
            ui->writeCombinedFileToSIMMButton->setEnabled(false);
            ui->saveCombinedFileButton->setEnabled(false);
        }
        else if (!supportsCompression && alreadyCompressed)
        {
            // If they choose a compressed disk image and a ROM that doesn't support compression,
            // detect that edge case
            ui->createROMErrorText->setText("ROM doesn't support compression, but disk image is compressed");
            error = true;
            ui->writeCombinedFileToSIMMButton->setEnabled(false);
            ui->saveCombinedFileButton->setEnabled(false);
        }
        else if (identifyBaseROM() == BaseROMbbraun2MB && uncompressedImage.length() > 1572864)
        {
            ui->createROMErrorText->setText("This base ROM only supports disk images " + QLocale(QLocale::English).toString(1572864) + " bytes or less in size.");
            error = true;
            ui->writeCombinedFileToSIMMButton->setEnabled(false);
            ui->saveCombinedFileButton->setEnabled(false);
        }
        else
        {
            QFileInfo baseRom(ui->chosenBaseROMFile->text());
            qint64 size = baseRom.size();

            if (shouldCompress)
            {
                size += compressedImage.length();
            }
            else
            {
                size += uncompressedImage.length();
            }

            QString prettySize = displayableFileSize(size);
            if (shouldCompress || alreadyCompressed)
            {
                prettySize += " (FC8-compressed)";
            }

            const qint64 simmSize = simmTable[ui->simmCapacityBox->currentIndex()].size * 1024;
            if (size > simmSize)
            {
                // If the image is too big, it's an error
                error = true;
                ui->writeCombinedFileToSIMMButton->setEnabled(false);
                ui->saveCombinedFileButton->setEnabled(false);

                // Mark just how much too big it is.
                prettySize += ". Disk image is too large by " + QLocale(QLocale::English).toString(size - simmSize) + " bytes.";
            }
            else
            {
                prettySize += ". " + QLocale(QLocale::English).toString(simmSize - size) + " bytes of space remain.";
            }

            ui->createROMErrorText->setText("Total ROM Size: " + prettySize);
        }
    }
    else
    {
        // Something's not valid, but it's not a critical error.
        // Typically means both files haven't been chosen yet.
        ui->createROMErrorText->setText("");
        error = false;
    }

    // Show errors in red
    if (error)
    {
        ui->createROMErrorText->setStyleSheet("color: red;");
    }
    else
    {
        ui->createROMErrorText->setStyleSheet("");
    }
}

bool MainWindow::checkBaseROMValidity(QString &errorText)
{
    const QString baseROMFileName = ui->chosenBaseROMFile->text();
    QFileInfo fi(baseROMFileName);
    if (baseROMFileName.isEmpty() || !fi.exists() || !fi.isFile())
    {
        errorText = "";
        return false;
    }

    // Check to make sure it's actually a ROM file that has disk image capabilities.
    // Easiest way to do this is look for the hack near 0x1700.
    if (fi.size() != 512*1024)
    {
        errorText = "The selected base ROM is not exactly 512 KB in size.";
        return false;
    }

    QFile f(baseROMFileName);
    if (!f.open(QFile::ReadOnly))
    {
        errorText = "Unable to open the base ROM.";
        return false;
    }

    QByteArray romData = f.read(0x1708);
    f.close();

    if (romData.length() != 0x1708)
    {
        errorText = "Unable to read from base ROM.";
        return false;
    }

    if (romData.at(0x1706) == 0x4E && romData.at(0x1707) == 0x75)
    {
        // If there's an RTS here, it's probably a ROM image, but it hasn't
        // been hacked to add the ROM disk driver.
        errorText = "The chosen base ROM isn't compatible with ROM disks.";
        return false;
    }
    else if (romData.at(0x1706) != 0x67)
    {
        // If there isn't a BEQ instruction here, it's likely not a ROM image,
        // hacked or otherwise.
        errorText = "The chosen base ROM doesn't appear to be a Mac ROM image.";
        return false;
    }

    // Basic sanity check is now done. Theres's a BEQ instruction where we would
    // expect to find it in a ROM that has ROM disk support.
    return true;
}

bool MainWindow::checkBaseROMCompressionSupport()
{
    // This string shows up in custom ROMs that support compression
    return unpatchedBaseROM().contains(" block-compressed disk image");
}

MainWindow::KnownBaseROM MainWindow::identifyBaseROM()
{
    QByteArray baseROM = unpatchedBaseROM();

    if (baseROM.contains("Garrett's Workshop ROM Disk"))
    {
        return BaseROMGarrettsWorkshop;
    }
    else if (baseROM.contains(" block-compressed disk image"))
    {
        return BaseROMBMOW;
    }
    // Look for a known byte pattern in bbraun's ROM disk driver
    else if (baseROM.at(0x51DC0) == static_cast<char>(0x4E) &&
             baseROM.at(0x51DC1) == static_cast<char>(0xBA) &&
             baseROM.at(0x51DC2) == static_cast<char>(0x04) &&
             baseROM.at(0x51DC3) == static_cast<char>(0xDC))
    {
        return BaseROMbbraun8MB;
    }
    // The pattern is slightly different in the 2 MB version
    else if (baseROM.at(0x51DC0) == static_cast<char>(0x4E) &&
             baseROM.at(0x51DC1) == static_cast<char>(0xBA) &&
             baseROM.at(0x51DC2) == static_cast<char>(0x03) &&
             baseROM.at(0x51DC3) == static_cast<char>(0x02))
    {
        return BaseROMbbraun2MB;
    }

    return BaseROMUnknown;
}

bool MainWindow::checkDiskImageValidity(QString &errorText, bool &alreadyCompressed)
{
    alreadyCompressed = false;

    const QString diskImageFileName = ui->chosenDiskImageFile->text();
    QFileInfo fi(diskImageFileName);
    if (diskImageFileName.isEmpty() || !fi.exists() || !fi.isFile())
    {
        errorText = "";
        return false;
    }

    // For now, validate disk images by ensuring they begin with boot blocks "LK"
    // as well as an HFS disk image afterward "BD"
    if (fi.size() < 1026)
    {
        errorText = "The selected disk image is too small to be a disk image.";
        return false;
    }

    QFile f(diskImageFileName);
    if (!f.open(QFile::ReadOnly))
    {
        errorText = "Unable to open the disk image.";
        return false;
    }

    QByteArray diskImageData = f.read(1026);
    f.close();

    if (diskImageData.length() != 1026)
    {
        errorText = "Unable to read from disk image.";
        return false;
    }

    // This image is already compressed (FC8 block mode or whole mode)
    if (isCompressedDiskImage(diskImageData))
    {
        alreadyCompressed = true;
        // Assume compressed images are usable. We don't want to decompress
        // just to find out. Not worth the effort.
        return true;
    }

    if (diskImageData.at(1024) != 'B' || diskImageData.at(1025) != 'D')
    {
        errorText = "The chosen disk image is not an HFS disk image.";
        return false;
    }

    if (diskImageData.at(0) != 'L' || diskImageData.at(1) != 'K')
    {
        errorText = "The chosen disk image doesn't have boot blocks.";
        return false;
    }

    return true;
}

bool MainWindow::isCompressedDiskImage(const QByteArray &image)
{
    // Look for start of FC8b or FC8_
    return image.length() >= 4 && image.at(0) == 'F' &&
        image.at(1) == 'C' && image.at(2) == '8' &&
        (image.at(3) == 'b' || image.at(3) == '_');
}

void MainWindow::compressImageInBackground(QByteArray uncompressedImage, bool blockUntilCompletion)
{
    // Set up a thread to do the compression in the background. It can take a few seconds.
    QThread *compressionThread = new QThread();
    FC8Compressor *compressor = new FC8Compressor(uncompressedImage, 65536);
    compressor->moveToThread(compressionThread);
    // When the compression finishes, save it in this object. Just doing this to make use of
    // cross-thread signal functionality.
    connect(compressor, SIGNAL(compressionFinished(QByteArray,QByteArray)), this, SLOT(compressorThreadFinished(QByteArray,QByteArray)));
    // When the compression finishes, delete the compressor
    connect(compressor, SIGNAL(compressionFinished(QByteArray,QByteArray)), compressor, SLOT(deleteLater()));
    // When the compressor is destroyed, stop the thread
    connect(compressor, SIGNAL(destroyed()), compressionThread, SLOT(quit()));
    // When the thread starts, start the compressor
    connect(compressionThread, SIGNAL(started()), compressor, SLOT(doCompression()));
    // When the thread stops, it should destroy itself
    connect(compressionThread, SIGNAL(finished()), compressionThread, SLOT(deleteLater()));

    compressionThread->start();

    if (blockUntilCompletion)
    {
        QWidget *prevPage = ui->pages->currentWidget();
        ui->progressBar->setRange(0, 0);
        ui->statusLabel->setText("Compressing disk image...");
        ui->pages->setCurrentWidget(ui->statusPage);

        // Block until the compression is complete, showing a progress bar
        while (!compressionThread->isFinished() ||
               !FC8Compressor::hashMatchesFile(compressedImageFileHash, uncompressedImage))
        {
            qApp->processEvents();
        }

        // Restore the page we were on before
        ui->pages->setCurrentWidget(prevPage);
    }
}

QByteArray MainWindow::uncompressedDiskImage()
{
    const QString diskImageFileName = ui->chosenDiskImageFile->text();
    QFile f(diskImageFileName);
    if (!f.open(QFile::ReadOnly))
    {
        return QByteArray();
    }
    QByteArray data = f.readAll();
    f.close();

    return data;
}

QByteArray MainWindow::diskImageToWrite()
{
    QByteArray uncompressedImage = uncompressedDiskImage();

    // If the selected ROM doesn't support compression, return it uncompressed.
    // Also, if it's a compressed disk image, return it as is (earlier checks
    // will have already ensured we are using a supported ROM in that case)
    if (!checkBaseROMCompressionSupport() || isCompressedDiskImage(uncompressedImage))
    {
        return uncompressedImage;
    }

    // Otherwise, return the compressed image which we should have already
    // verified is good to go. Double check though...it's possible that the file
    // changed underneath us, in which case we need to compress it again.
    if (FC8Compressor::hashMatchesFile(compressedImageFileHash, uncompressedImage))
    {
        return compressedImage;
    }
    else
    {
        // It doesn't match, which means the filename hasn't changed but the
        // content has changed since we last compressed it. Recompress it.
        compressImageInBackground(uncompressedImage, true);

        // Make sure it matches now
        if (FC8Compressor::hashMatchesFile(compressedImageFileHash, uncompressedImage))
        {
            return compressedImage;
        }
        else
        {
            // Somehow we failed to recompress it; cause an error to be returned.
            // Should never happen; just for safeguarding.
            return QByteArray();
        }
    }
}

QByteArray MainWindow::unpatchedBaseROM()
{
    QByteArray finalImage;
    QFile f(ui->chosenBaseROMFile->text());
    if (!f.open(QFile::ReadOnly))
    {
        return QByteArray();
    }
    finalImage = f.readAll();
    f.close();
    return finalImage;
}

QByteArray MainWindow::patchedBaseROM()
{
    QByteArray rom = unpatchedBaseROM();
    uint32_t imageSize = uncompressedDiskImage().length();

    // If we find a base ROM that we know how to modify for the correct disk image size,
    // perform the modification here.
    switch (identifyBaseROM())
    {
    case BaseROMbbraun8MB:
        rom[0x52500] = (imageSize >> 24) & 0xFF;
        rom[0x52501] = (imageSize >> 16) & 0xFF;
        rom[0x52502] = (imageSize >> 8) & 0xFF;
        rom[0x52503] = (imageSize >> 0) & 0xFF;

        // bbraun's 8 MB 0.9.6 base image has a bug that can cause a bus error when booting with R+A
        // if a write is attempted before the ROM disk has been copied to RAM. Work around this
        // bug if the driver exactly matches the known broken driver. The fix is, when deciding if
        // a write operation is allowed or not, to look at origdisk instead of drvsts.writeProt.
        // writeProt can say the drive is writable even though it hasn't been copied to RAM yet.
        // When origdisk is non-null, we're guaranteed it's in RAM, so it's a safer check.
        if (QCryptographicHash::hash(rom.mid(0x51D40, 0x7BC), QCryptographicHash::Md5) ==
            QByteArray("\x0E\x12\x43\x36\x03\x48\x5C\xDE\x2E\x4C\x04\xE3\x30\xF9\xD2\x0B", 16))
        {
            // Change opcode from tst.b to tst.l
            rom[0x521F1] = 0xAA;
            // Change tested data from drvsts.writeProt to origdisk
            rom[0x521F3] = 0x22;
            // Change bne to beq
            rom[0x521F4] = 0x67;
        }
        break;
    case BaseROMGarrettsWorkshop:
        rom[0x51DAC] = (imageSize >> 24) & 0xFF;
        rom[0x51DAD] = (imageSize >> 16) & 0xFF;
        rom[0x51DAE] = (imageSize >> 8) & 0xFF;
        rom[0x51DAF] = (imageSize >> 0) & 0xFF;
        break;
    case BaseROMUnknown:
    case BaseROMbbraun2MB:
    case BaseROMBMOW:
    default:
        break;
    }

    return rom;
}

QByteArray MainWindow::createROM()
{
    QByteArray finalImage;
    QByteArray baseROM = patchedBaseROM();
    if (baseROM.isEmpty())
    {
        return QByteArray();
    }

    finalImage = baseROM;

    QByteArray diskImage = diskImageToWrite();
    if (diskImage.isEmpty())
    {
        return QByteArray();
    }
    finalImage += diskImage;

    return finalImage;
}

QString MainWindow::displayableFileSize(qint64 size)
{
    if (size < 1048576)
    {
        if (size % 1024 == 0)
        {
            return QString("%1 KB").arg(size / 1024);
        }
        else
        {
            float sizeF = static_cast<float>(size) / 1024.0f;
            return QString("%1 KB").arg(sizeF, 0, 'f', 1);
        }
    }
    else
    {
        if (size % 1048576 == 0)
        {
            return QString("%1 MB").arg(size / 1048576);
        }
        else
        {
            float sizeF = static_cast<float>(size) / 1048576.0f;
            return QString("%1 MB").arg(sizeF, 0, 'f', 1);
        }
    }
}

void MainWindow::on_writeCombinedFileToSIMMButton_clicked()
{
    QByteArray dataToWrite = createROM();
    if (dataToWrite.isEmpty())
    {
        showMessageBox(QMessageBox::Warning, "Error combining files", "The ROM and disk image were unable to be combined. Make sure you chose the correct files.");
        return;
    }

    QBuffer *combinedFile = new QBuffer();
    combinedFile->setData(dataToWrite);
    doInternalWrite(combinedFile);
}

void MainWindow::on_saveCombinedFileButton_clicked()
{
    QByteArray dataToWrite = createROM();
    if (dataToWrite.isEmpty())
    {
        showMessageBox(QMessageBox::Warning, "Error combining files", "The ROM and disk image were unable to be combined. Make sure you chose the correct files.");
        return;
    }

    QString filename = QFileDialog::getSaveFileName(this, "Save combined ROM and disk image as:");
    if (!filename.isNull())
    {
        QFile f(filename);
        if (!f.open(QFile::WriteOnly))
        {
            showMessageBox(QMessageBox::Warning, "Error opening output file", "Unable to open file for writing. Make sure you have correct file permissions.");
            return;
        }

        bool success = f.write(dataToWrite) == dataToWrite.length();
        f.close();

        if (success)
        {
            showMessageBox(QMessageBox::Information, "Save complete", "The combined ROM image was saved successfully.");
        }
        else
        {
            showMessageBox(QMessageBox::Warning, "Error writing output file", "Unable to save combined ROM image.");
        }
    }
}

void MainWindow::compressorThreadFinished(QByteArray hashOfOriginal, QByteArray compressedData)
{
    compressedImageFileHash = hashOfOriginal;
    compressedImage = compressedData;
    updateCreateROMControlStatus();
}

bool MainWindow::firmwareIsCompatible(QString filename, QString &compatibilityError)
{
    QFile fwFile(filename);
    if (!fwFile.open(QFile::ReadOnly))
    {
        compatibilityError = "Unable to open the selected firmware file.";
        return false;
    }

    QByteArray firmware = fwFile.readAll();
    fwFile.close();

    // Find the device descriptor in the dump. Locate it by
    // searching for the USB VID and PID, and then double-checking
    // that it's actually a device descriptor by verifying the surrounding data.
    QByteArray vidPid("\xD0\x16\xAA\x06", 4);
    int index = 0;
    int descriptorsFound = 0;
    do
    {
        index = firmware.indexOf(vidPid, index);
        if (index >= 0)
        {
            // Is this actually the device descriptor? Check a bunch of fields to see.
            // The descriptors are slightly different between revisions, but this will
            // check enough of the fields that we can be confident.
            if (index >= 8 &&
                index + 9 < firmware.length() &&
                firmware.at(index - 8) == 0x12 && // Device descriptor length
                firmware.at(index - 7) == 0x01 && // Device descriptor identifier
                firmware.at(index - 4) == 0x02 && // Class = CDC communication device
                firmware.at(index - 3) == 0x00 && // Subclass = 0
                firmware.at(index - 2) == 0x00 && // Protocol = 0)
                firmware.at(index + 6) == 0x01 && // Manufacturer string = 1
                firmware.at(index + 7) == 0x02 && // Product string = 2
                firmware.at(index + 9) == 0x01) // Num configurations = 1
            {
                // We're pretty sure it is. Let's extract the revision.
                uint16_t revision = static_cast<uint8_t>(firmware.at(index + 4)) |
                                    static_cast<uint8_t>(firmware.at(index + 5)) << 8;

                // See if it matches the current programmer revision. Alternatively,
                // if the current programmer revision is 0 it means we failed to detect it,
                // and we should just let them attempt it anyway.
                if (revision == p->programmerRevision() ||
                    p->programmerRevision() == ProgrammerRevisionUnknown)
                {
                    return true;
                }

                // Count how many descriptors we found
                descriptorsFound++;
            }

            index += 4;
        }
    } while (index >= 0);

    if (descriptorsFound == 0)
    {
        compatibilityError = "The selected file doesn't appear to be a programmer firmware file.";
    }
    else
    {
        compatibilityError = "The selected file is a SIMM programmer firmware file, "
                             "but it isn't compatible with your programmer.\n\n"
                             "Please download the correct firmware file from: "
                             "https://github.com/dougg3/mac-rom-simm-programmer/releases";
    }
    return false;
}

void MainWindow::showMessageBox(QMessageBox::Icon icon, const QString &title, const QString &text)
{
    // We can't show multiple message boxes
    if (activeMessageBox) { return; }

    // Show the message box as a modal dialog, connect so we get notified when they click OK
    activeMessageBox = new QMessageBox(icon, title, text, QMessageBox::Ok, this);
    connect(activeMessageBox, SIGNAL(finished(int)), this, SLOT(messageBoxFinished()));
    activeMessageBox->setModal(true);
    activeMessageBox->open();
}

void MainWindow::messageBoxFinished()
{
    if (activeMessageBox)
    {
        activeMessageBox->deleteLater();
        activeMessageBox = NULL;
    }
}

void MainWindow::on_actionExtended_UI_triggered(bool checked)
{
    setUseExtendedUI(checked);
}

void MainWindow::setUseExtendedUI(bool extended)
{
    const bool alreadyExtended = ui->tabWidget->isHidden();
    if (extended == alreadyExtended) { return; }

    if (extended)
    {
        // Put everything on one page
        ui->controlLayout->insertWidget(1, ui->createROMGroupBox);
        ui->controlLayout->insertWidget(2, ui->writeGroupBox);
        ui->controlLayout->insertWidget(3, ui->readGroupBox);
        ui->writeGroupBox->setTitle(ui->tabWidget->tabText(0));
        ui->createROMGroupBox->setTitle(ui->tabWidget->tabText(1));
        ui->readGroupBox->setTitle(ui->tabWidget->tabText(2));
        ui->tabWidget->hide();
        ui->createVerifyBox->hide();
        ui->createHowMuchToWriteBox->hide();
    }
    else
    {
        // Restore widgets to the original layout in the UI file
        ui->createTabLayout->insertWidget(0, ui->createROMGroupBox);
        ui->writeTabLayout->insertWidget(0, ui->writeGroupBox);
        ui->readTabLayout->insertWidget(0, ui->readGroupBox);
        ui->writeGroupBox->setTitle("");
        ui->createROMGroupBox->setTitle("");
        ui->readGroupBox->setTitle("");
        ui->tabWidget->show();
        ui->createVerifyBox->show();
        ui->createHowMuchToWriteBox->show();
    }

    if (!initializing)
    {
        QSettings settings;
        settings.setValue(extendedViewKey, extended);
    }
}

void MainWindow::on_actionCreate_blank_disk_image_triggered()
{
    CreateBlankDiskDialog dialog;
    int result = dialog.exec();
    if (result == QDialog::Accepted)
    {
        QString filename = QFileDialog::getSaveFileName(this, "Save blank disk image as:", QString(), "Disk images (*.dsk)");
        if (!filename.isEmpty())
        {
            QFile outFile(filename);
            if (outFile.open(QFile::WriteOnly))
            {
                QByteArray be(dialog.selectedDiskSize(), static_cast<char>(0));
                bool success = outFile.write(be) == be.length();
                outFile.close();

                if (success)
                {
                    QMessageBox::information(this, "Disk image created", "Successfully created a blank disk image.");
                }
                else
                {
                    QMessageBox::warning(this, "File write error", "Unable to write to the blank disk image file.");
                }
            }
            else
            {
                QMessageBox::warning(this, "File save error", "Unable to open the blank disk image file for writing.");
            }
        }
    }
}
