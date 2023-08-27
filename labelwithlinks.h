#ifndef LABELWITHLINKS_H
#define LABELWITHLINKS_H

#include <QLabel>

class LabelWithLinks : public QLabel
{
    Q_OBJECT
public:
    explicit LabelWithLinks(QWidget *parent = NULL);

protected:
    virtual void changeEvent(QEvent *event);
};

#endif // LABELWITHLINKS_H
