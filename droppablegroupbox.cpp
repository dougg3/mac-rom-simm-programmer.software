#include "droppablegroupbox.h"
#include <QDropEvent>
#include <QMimeData>
#include <QDebug>
#include <QUrl>

DroppableGroupBox::DroppableGroupBox(QWidget *parent) :
    QGroupBox(parent)
{
    setAcceptDrops(true);
}

void DroppableGroupBox::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() == 1 &&
            urls[0].isLocalFile() &&
            QFile::exists(urls[0].toLocalFile()))
        {
            event->accept();
        }
    }
}

void DroppableGroupBox::dropEvent(QDropEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() == 1 &&
            urls[0].isLocalFile() &&
            QFile::exists(urls[0].toLocalFile()))
        {
            event->accept();
            emit fileDropped(urls[0].toLocalFile());
        }
    }
}
