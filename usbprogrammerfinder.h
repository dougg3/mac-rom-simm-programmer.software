#ifndef USBPROGRAMMERFINDER_H
#define USBPROGRAMMERFINDER_H

#include <QObject>

#define USB_PROGRAMMER_VID  0x03EB
#define USB_PROGRAMMER_PID  0x204B

class USBProgrammerFinder : public QObject
{
    Q_OBJECT
public:
    explicit USBProgrammerFinder(QObject *parent = 0);
    QString getSerialPortName();
signals:
    void programmerPresenceChanged(bool);
public slots:
    
};

#endif // USBPROGRAMMERFINDER_H
