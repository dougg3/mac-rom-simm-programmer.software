#ifndef TEXTBROWSERWITHLINKS_H
#define TEXTBROWSERWITHLINKS_H

#include <QTextBrowser>

class TextBrowserWithLinks : public QTextBrowser
{
    Q_OBJECT
public:
    explicit TextBrowserWithLinks(QWidget *parent = NULL);
    void setHtml(QString const &text);

protected:
    virtual void changeEvent(QEvent *event);

private:
    QString _originalHtml;
};

#endif // TEXTBROWSERWITHLINKS_H
