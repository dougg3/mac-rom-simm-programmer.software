#ifndef DROPPABLEGROUPBOX_H
#define DROPPABLEGROUPBOX_H

#include <QGroupBox>

class DroppableGroupBox : public QGroupBox
{
    Q_OBJECT
public:
    DroppableGroupBox(QWidget *parent = 0);
    void setMaxFiles(int maxFiles) { _maxFiles = maxFiles; }

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

signals:
    void fileDropped(QString path);

private:
    int _maxFiles;
};

#endif // DROPPABLEGROUPBOX_H
