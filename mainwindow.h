#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
//#include <qextserialenumerator.h>
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

    void programmerWriteStatusChanged(WriteStatus newStatus);
    void programmerWriteTotalLengthChanged(uint32_t totalLen);
    void programmerWriteCompletionLengthChanged(uint32_t len);

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

private:
    Ui::MainWindow *ui;
    bool writeFileValid;
    bool readFileValid;
    QString electricalTestString;

    void resetAndShowStatusPage();
};

#endif // MAINWINDOW_H
