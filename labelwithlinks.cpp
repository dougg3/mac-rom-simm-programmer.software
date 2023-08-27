#include "labelwithlinks.h"
#include <QEvent>

LabelWithLinks::LabelWithLinks(QWidget *parent) :
    QLabel(parent)
{

}

void LabelWithLinks::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        QString tmp = text();
        setText("");
        setText(tmp);
    }
}
