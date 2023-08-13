#ifndef CREATEBLANKDISKDIALOG_H
#define CREATEBLANKDISKDIALOG_H

#include <QDialog>

namespace Ui {
class CreateBlankDiskDialog;
}

class CreateBlankDiskDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateBlankDiskDialog(QWidget *parent = NULL);
    ~CreateBlankDiskDialog();

    int selectedDiskSize() const;

private slots:
    void on_sizeBox_currentIndexChanged(int index);
    void on_customSizeBytesSpinner_valueChanged(int value);
    void on_customSizeMBSpinner_valueChanged(double value);
    void on_fixButton_clicked();

private:
    bool setComboBoxItemEnabled(int index, bool enabled);
    int truncateToNearest512(int size);
    void updateFixSectionVisibility();

    Ui::CreateBlankDiskDialog *ui;
};

#endif // CREATEBLANKDISKDIALOG_H
