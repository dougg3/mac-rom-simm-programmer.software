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
    void on_readFromSIMMButton_clicked();

    void on_chosenWriteFile_textEdited(const QString &newText);
    void on_chosenReadFile_textEdited(const QString &newText);

    void on_writeGroupBox_fileDropped(const QString &filePath);
    void on_readGroupBox_fileDropped(const QString &filePath);

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

    void on_electricalTestButton_clicked();

    void on_actionUpdate_firmware_triggered();

    void on_identifyButton_clicked();

    // Handlers for when the programmer board has been connected or disconnected
    void programmerBoardConnected();
    void programmerBoardDisconnected();
    void programmerBoardDisconnectedDuringOperation();

    void on_simmCapacityBox_currentIndexChanged(int index);

    void on_actionAbout_SIMM_Programmer_triggered();

    void on_verifyBox_currentIndexChanged(int index);

    void on_howMuchToWriteBox_currentIndexChanged(int index);

    void on_flashIndividualEnterButton_clicked();
    void on_returnNormalButton_clicked();

    void updateFlashIndividualControlsEnabled();
    void selectIndividualWriteFileClicked();
    void selectIndividualReadFileClicked();

    void on_multiFlashChipsButton_clicked();
    void on_multiReadChipsButton_clicked();
    void finishMultiRead();

private:
    Ui::MainWindow *ui;
    bool initializing;
    bool writeFileValid;
    bool readFileValid;
    QFile *writeFile;
    QFile *readFile;
    QString electricalTestString;
    QBuffer *writeBuffer;
    QBuffer *readBuffer;

    void resetAndShowStatusPage();
    void handleVerifyFailureReply();

    void hideFlashIndividualControls();
    void showFlashIndividualControls();

    void returnToControlPage();
};

#endif // MAINWINDOW_H
