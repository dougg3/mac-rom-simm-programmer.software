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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFile>
#include <QMessageBox>
#include "programmer.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_selectWriteFileButton_clicked();
    void on_selectReadFileButton_clicked();

    void on_writeToSIMMButton_clicked();
    void doInternalWrite(QIODevice *device);
    void on_readFromSIMMButton_clicked();

    void on_chosenWriteFile_textEdited(const QString &newText);
    void on_chosenReadFile_textEdited(const QString &newText);

    void on_writeGroupBox_fileDropped(const QString &filePath);
    void on_readGroupBox_fileDropped(const QString &filePath);
    void on_createROMGroupBox_fileDropped(const QString &filePath);

    void programmerWriteStatusChanged(WriteStatus newStatus);
    void programmerWriteTotalLengthChanged(uint32_t totalLen);
    void programmerWriteCompletionLengthChanged(uint32_t len);

    void programmerVerifyTotalLengthChanged(uint32_t totalLen);
    void programmerVerifyCompletionLengthChanged(uint32_t len);

    void programmerElectricalTestStatusChanged(ElectricalTestStatus newStatus);
    void programmerElectricalTestLocation(uint8_t loc1, uint8_t loc2);

    void programmerReadStatusChanged(ReadStatus newStatus);
    void programmerReadTotalLengthChanged(uint32_t totalLen);
    void programmerReadCompletionLengthChanged(uint32_t len);

    void programmerIdentifyStatusChanged(IdentificationStatus newStatus);

    void programmerFirmwareFlashStatusChanged(FirmwareFlashStatus newStatus);
    void programmerFirmwareFlashTotalLengthChanged(uint32_t totalLen);
    void programmerFirmwareFlashCompletionLengthChanged(uint32_t len);

    void programmerFirmwareVersionStatusChanged(ReadFirmwareVersionStatus status, uint32_t version);

    void on_electricalTestButton_clicked();

    void on_actionUpdate_firmware_triggered();

    void on_identifyButton_clicked();

    // Handlers for when the programmer board has been connected or disconnected
    void programmerBoardConnected();
    void programmerBoardDisconnected();
    void programmerBoardDisconnectedDuringOperation();

    void on_simmCapacityBox_currentIndexChanged(int index);

    void on_actionAbout_SIMM_Programmer_triggered();
    void on_actionCheck_Firmware_Version_triggered();

    void on_verifyBox_currentIndexChanged(int index);
    void on_createVerifyBox_currentIndexChanged(int index);

    void on_howMuchToWriteBox_currentIndexChanged(int index);
    void on_createHowMuchToWriteBox_currentIndexChanged(int index);

    void on_flashIndividualEnterButton_clicked();
    void on_returnNormalButton_clicked();

    void updateFlashIndividualControlsEnabled();
    void selectIndividualWriteFileClicked();
    void selectIndividualReadFileClicked();

    void on_multiFlashChipsButton_clicked();
    void on_multiReadChipsButton_clicked();
    void finishMultiRead();

    void on_verifyROMChecksumButton_clicked();
    void finishChecksumVerify();
    bool calculateROMChecksum(QByteArray const &rom, uint32_t len, uint32_t &checksum);

    void on_selectBaseROMButton_clicked();
    void on_selectDiskImageButton_clicked();
    void on_chosenBaseROMFile_textEdited(const QString &text);
    void on_chosenDiskImageFile_textEdited(const QString &text);
    void updateCreateROMControlStatus();
    void on_writeCombinedFileToSIMMButton_clicked();
    void on_saveCombinedFileButton_clicked();

    void compressorThreadFinished(QByteArray hashOfOriginal, QByteArray compressedData);

    void messageBoxFinished();

    void on_actionExtended_UI_triggered(bool checked);

    void on_actionCreate_blank_disk_image_triggered();

private:
    Ui::MainWindow *ui;
    bool initializing;
    QIODevice *writeFile;
    QFile *readFile;
    QString electricalTestString;
    QBuffer *writeBuffer;
    QBuffer *readBuffer;
    QBuffer *checksumVerifyBuffer;
    QByteArray compressedImageFileHash;
    QByteArray compressedImage;
    QMessageBox *activeMessageBox;

    enum KnownBaseROM
    {
        BaseROMUnknown,
        BaseROMbbraun2MB,
        BaseROMbbraun8MB,
        BaseROMBMOW,
        BaseROMGarrettsWorkshop,
        BaseROMbbraunInQuadra,
    };

    void resetAndShowStatusPage();
    void handleVerifyFailureReply();

    void hideFlashIndividualControls();
    void showFlashIndividualControls();

    void returnToControlPage();

    bool checkBaseROMValidity(QString &errorText);
    bool checkBaseROMCompressionSupport();
    KnownBaseROM identifyBaseROM(QByteArray const *baseROMToCheck = NULL);
    int offsetToQuadraROMDiskSize(QByteArray const &baseROM);
    bool checkDiskImageValidity(QString &errorText, bool &alreadyCompressed);
    bool isCompressedDiskImage(QByteArray const &image);
    void compressImageInBackground(QByteArray uncompressedImage, bool blockUntilCompletion);
    QByteArray uncompressedDiskImage();
    QByteArray diskImageToWrite();
    QByteArray unpatchedBaseROM();
    QByteArray patchedBaseROM();
    QByteArray createROM();
    QString displayableFileSize(qint64 size);

    static QList<QByteArray> separateFirmwareIntoVersions(QByteArray totalFirmware);
    QByteArray findCompatibleFirmware(QString filename, QString &compatibilityError);
    bool firmwareIsCompatible(QByteArray const &firmware, bool &isFirmwareFile);

    void showMessageBox(QMessageBox::Icon icon, const QString &title, const QString &text);
    void setUseExtendedUI(bool extended);
};

#endif // MAINWINDOW_H
