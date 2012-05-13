//#ifdef Q_WS_WIN

#include "usbprogrammerfinder.h"
#include <windows.h>
#include <setupapi.h>
//#include <tchar.h>

USBProgrammerFinder::USBProgrammerFinder(QObject *parent) :
    QObject(parent)
{
}

QString USBProgrammerFinder::getSerialPortName()
{
    /*//GUID_
    HDEVINFO result = SetupDiGetClassDevs(NULL, L"USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);

    if (result != INVALID_HANDLE_VALUE)
    {

    }
    else
    {
        return QString::null;
    }

    return QString();*/

    return "COM13";
}

//#endif
