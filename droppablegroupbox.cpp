#include "droppablegroupbox.h"
#include <QDropEvent>
#include <QMimeData>
#include <QDebug>
#include <QUrl>

DroppableGroupBox::DroppableGroupBox(QWidget *parent) :
    QGroupBox(parent),
    _maxFiles(1)
{
    setAcceptDrops(true);
}

void DroppableGroupBox::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() > 0 &&
            urls.count() <= _maxFiles)
        {
            foreach (QUrl const &url, urls)
            {
                if (!url.isLocalFile() ||
                !QFile::exists(url.toLocalFile()))
                {
                    return;
                }
            }
            event->accept();
        }
    }
}

void DroppableGroupBox::dropEvent(QDropEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() > 0 &&
            urls.count() <= _maxFiles)
        {
            // Make sure we're accepting the drop
            foreach (QUrl const &url, urls)
            {
                if (!url.isLocalFile() ||
                !QFile::exists(url.toLocalFile()))
                {
                    return;
                }
            }

            foreach (QUrl const &url, urls)
            {
                emit fileDropped(url.toLocalFile());
            }

            event->accept();
        }
    }
}
