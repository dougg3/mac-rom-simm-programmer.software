#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "programmer.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>

static Programmer *p;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    p = new Programmer();
    ui->chosenWriteFile->setText("");
    ui->chosenReadFile->setText("");
    writeFileValid = false;
    readFileValid = false;
    ui->writeToSIMMButton->setEnabled(false);
    ui->readFromSIMMButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setEnabled(false);
    ui->statusLabel->setText("");
    ui->cancelButton->setEnabled(false);

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

    /*QList<QextPortInfo> l = QextSerialEnumerator::getPorts();
    foreach (QextPortInfo p, l)
    {
        qDebug() << "Found port...";
        qDebug() << "Enum name:" << p.enumName;
        qDebug() << "Friend name:" << p.friendName;
        qDebug() << "Phys name:" << p.physName;
        qDebug() << "Port name:" << p.portName;
        qDebug() << "Product ID:" << hex << p.productID;
        qDebug() << "Vendor ID:" << hex << p.vendorID;
        qDebug() << "";

#ifdef Q_WS_WIN
        if (p.portName.startsWith("COM"))
#endif
        {
            // TODO: Do checking for valid USB ID?

            if ((p.vendorID == 0x03EB) && (p.productID == 0x204B))
            {
                ui->portList->addItem(QString("%1 (Programmer)").arg(p.portName), QVariant(p.portName));
            }
            else
            {
                ui->portList->addItem(p.portName, QVariant(p.portName));
            }
        }
    }*/

    QextSerialEnumerator *p = new QextSerialEnumerator();
    connect(p, SIGNAL(deviceDiscovered(QextPortInfo)), SLOT(portDiscovered(QextPortInfo)));
    connect(p, SIGNAL(deviceRemoved(QextPortInfo)), SLOT(portRemoved(QextPortInfo)));
    p->setUpNotifications();
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
    p->ReadSIMMToFile(ui->chosenReadFile->text());
    qDebug() << "Reading from SIMM...";
}

void MainWindow::on_writeToSIMMButton_clicked()
{
    p->WriteFileToSIMM(ui->chosenWriteFile->text());
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
    case WriteComplete:
        QMessageBox::information(this, "Write complete", "The write operation finished.");
        break;
    case WriteError:
        QMessageBox::warning(this, "Write error", "An error occurred writing to the SIMM.");
        break;
    case WriteCancelled:
        QMessageBox::warning(this, "Write cancelled", "The write operation was cancelled.");
        break;
    case WriteEraseComplete:
        // No message needed for this
        break;
    case WriteEraseFailed:
        QMessageBox::warning(this, "Write error", "An error occurred erasing the SIMM.");
        break;
    case WriteTimedOut:
        QMessageBox::warning(this, "Write timed out", "The write operation timed out.");
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
    electricalTestString = "";
    p->RunElectricalTest();
}

void MainWindow::programmerElectricalTestStatusChanged(ElectricalTestStatus newStatus)
{
    switch (newStatus)
    {
    case ElectricalTestStarted:
        qDebug() << "Electrical test started";
        break;
    case ElectricalTestPassed:
        QMessageBox::information(this, "Test passed", "The electrical test passed successfully.");
        break;
    case ElectricalTestFailed:
        QMessageBox::warning(this, "Test failed", "The electrical test failed:\n\n" + electricalTestString);
        break;
    case ElectricalTestTimedOut:
        QMessageBox::warning(this, "Test timed out", "The electrical test operation timed out.");
        break;
    case ElectricalTestCouldntStart:
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
    case ReadComplete:
        QMessageBox::information(this, "Read complete", "The read operation finished.");
        break;
    case ReadError:
        QMessageBox::warning(this, "Read error", "An error occurred reading from the SIMM.");
        break;
    case ReadCancelled:
        QMessageBox::warning(this, "Read cancelled", "The read operation was cancelled.");
        break;
    case ReadTimedOut:
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
    case IdentificationComplete:
    {
        QString identifyString = "The chips identified themselves as:";
        for (int x = 0; x < 4; x++)
        {
            QString thisString;
            uint8_t manufacturer = 0;
            uint8_t device = 0;
            p->GetChipIdentity(x, &manufacturer, &device);
            thisString.sprintf("\nIC%d: Manufacturer 0x%02X, Device 0x%02X", (x + 1), manufacturer, device);
            identifyString.append(thisString);
        }

        QMessageBox::information(this, "Identification complete", identifyString);
        break;
    }
    case IdentificationError:
        QMessageBox::warning(this, "Identification error", "An error occurred identifying the chips on the SIMM.");
        break;
    case IdentificationTimedOut:
        QMessageBox::warning(this, "Identification timed out", "The identification operation timed out.");
        break;
    }
}

void MainWindow::programmerFirmwareFlashStatusChanged(FirmwareFlashStatus newStatus)
{
    switch (newStatus)
    {
    case FirmwareFlashComplete:
        QMessageBox::information(this, "Firmware update complete", "The firmware update operation finished.");
        break;
    case FirmwareFlashError:
        QMessageBox::warning(this, "Firmware update error", "An error occurred writing firmware to the device.");
        break;
    case FirmwareFlashCancelled:
        QMessageBox::warning(this, "Firmware update cancelled", "The firmware update was cancelled.");
        break;
    case FirmwareFlashTimedOut:
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

void MainWindow::startOperation_updateButtons()
{
    ui->writeToSIMMButton->setEnabled(false);
    ui->readFromSIMMButton->setEnabled(false);
    ui->identifyButton->setEnabled(false);
    ui->electricalTestButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->progressBar->setEnabled(true);
    ui->cancelButton->setEnabled(true);
}

void MainWindow::endOperation_updateButtons()
{
    ui->writeToSIMMButton->setEnabled(writeFileValid);
    ui->readFromSIMMButton->setEnabled(readFileValid);

    ui->identifyButton->setEnabled(true);
    ui->electricalTestButton->setEnabled(true);
    ui->progressBar->setValue(0);
    ui->progressBar->setEnabled(false);
    ui->cancelButton->setEnabled(false);
}


void MainWindow::on_actionUpdate_firmware_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this, "Select a firmware image:");
    if (!filename.isNull())
    {
        p->FlashFirmware(filename);
        qDebug() << "Updating firmware...";
    }
}

void MainWindow::on_identifyButton_clicked()
{
    p->IdentifySIMMChips();
}

void MainWindow::portDiscovered(const QextPortInfo & info)
{
    qDebug() << info.portName << "discovered";

#ifdef Q_WS_WIN
    if (info.portName.startsWith("COM"))
#endif
    {
        // TODO: Do checking for valid USB ID?

        if ((info.vendorID == 0x03EB) && (info.productID == 0x204B))
        {
            ui->portList->addItem(QString("%1 (Programmer)").arg(info.portName), QVariant(info.portName));
        }
        else
        {
            ui->portList->addItem(info.portName, QVariant(info.portName));
        }
    }
}

void MainWindow::portRemoved(const QextPortInfo & info)
{
    qDebug() << info.portName << "removed";

    for (int x = ui->portList->count() - 1; x >= 0; x--)
    {
        if (ui->portList->itemData(x) == info.portName)
        {
            ui->portList->removeItem(x);
        }
    }
}
