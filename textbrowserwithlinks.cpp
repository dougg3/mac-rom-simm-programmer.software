#include "textbrowserwithlinks.h"
#include <QEvent>

TextBrowserWithLinks::TextBrowserWithLinks(QWidget *parent) :
    QTextBrowser(parent)
{

}

void TextBrowserWithLinks::setHtml(const QString &text)
{
    _originalHtml = text;
    QTextBrowser::setHtml(text);
}

void TextBrowserWithLinks::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        QTextBrowser::setText(_originalHtml);
    }
}
