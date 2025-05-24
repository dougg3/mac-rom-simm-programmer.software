#include "createblankdiskdialog.h"
#include "ui_createblankdiskdialog.h"
#include <QStandardItemModel>

CreateBlankDiskDialog::CreateBlankDiskDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateBlankDiskDialog)
{
    ui->setupUi(this);

    // The window needs to be bigger on Mac and Linux due to larger fonts
#if defined(Q_OS_MACX) || defined(Q_OS_LINUX)
    resize(width() + 173, height() + 86);
#endif

    ui->buttonBox->addButton("Save...", QDialogButtonBox::AcceptRole);

    bool addedDisabledHeadings = false;

    // Add options for size. First, compressed items...
    ui->sizeBox->addItem("Compressed:");
    if (!setComboBoxItemEnabled(ui->sizeBox->count() - 1, false))
    {
        ui->sizeBox->removeItem(ui->sizeBox->count() - 1);
    }
    else
    {
        addedDisabledHeadings = true;
    }

    ui->sizeBox->addItem("2.35 MB (for 2 MB SIMM, compressed)", 2461696);
    ui->sizeBox->addItem("5.5 MB (for 4 MB SIMM, compressed)", 5767168);
    ui->sizeBox->addItem("12 MB (for 8 MB SIMM, compressed)", 12582912);

    // Next, uncompressed items...
    ui->sizeBox->addItem("Uncompressed:");
    if (!setComboBoxItemEnabled(ui->sizeBox->count() - 1, false))
    {
        ui->sizeBox->removeItem(ui->sizeBox->count() - 1);
    }
    ui->sizeBox->addItem("1.5 MB (for 2 MB SIMM, uncompressed)", 1572864);
    ui->sizeBox->addItem("3.5 MB (for 4 MB SIMM, uncompressed)", 3670016);
    ui->sizeBox->addItem("7.5 MB (for 8 MB SIMM, uncompressed)", 7864320);

    // And finally, for Quadra SIMMs:
    ui->sizeBox->addItem("Uncompressed, for 1 MB Quadra ROMs:");
    if (!setComboBoxItemEnabled(ui->sizeBox->count() - 1, false))
    {
        ui->sizeBox->removeItem(ui->sizeBox->count() - 1);
    }
    ui->sizeBox->addItem("1.0 MB (for 2 MB SIMM with Quadra ROM)", 1048576);
    ui->sizeBox->addItem("3.0 MB (for 4 MB SIMM with Quadra ROM)", 3145728);
    ui->sizeBox->addItem("7.0 MB (for 8 MB SIMM with Quadra ROM)", 7340032);

    // Finally, a section for custom items
    ui->sizeBox->addItem("Custom:");
    if (!setComboBoxItemEnabled(ui->sizeBox->count() - 1, false))
    {
        ui->sizeBox->removeItem(ui->sizeBox->count() - 1);
    }
    ui->sizeBox->addItem("Custom size (specified below)", 0);

    // If we were able to add disabled items, select the second item in the list
    // because the first item is a disabled heading
    if (addedDisabledHeadings)
    {
        ui->sizeBox->setCurrentIndex(1);
    }

    // Make sure everything is up to date
    on_sizeBox_currentIndexChanged(ui->sizeBox->currentIndex());
}

CreateBlankDiskDialog::~CreateBlankDiskDialog()
{
    delete ui;
}

int CreateBlankDiskDialog::selectedDiskSize() const
{
    return ui->customSizeBytesSpinner->value();
}

bool CreateBlankDiskDialog::setComboBoxItemEnabled(int index, bool enabled)
{
    QStandardItemModel *model = qobject_cast<QStandardItemModel *>(ui->sizeBox->model());
    if (!model) { return false; }

    QStandardItem *item = model->item(index);
    if (!item) { return false; }

    item->setEnabled(enabled);
    return true;
}

int CreateBlankDiskDialog::truncateToNearest512(int size)
{
    // We want the size to be a multiple of 512. Just truncate
    // it if we need to.
    if (size % 512 != 0)
    {
        size = (size / 512) * 512;
    }
    return size;
}

void CreateBlankDiskDialog::updateFixSectionVisibility()
{
    bool badSize = ui->customSizeBytesSpinner->value() % 512 != 0;
    ui->fix512BytesSection->setVisible(badSize);
    foreach (QAbstractButton *b, ui->buttonBox->buttons())
    {
        if (b->text() == "Save...")
        {
            b->setEnabled(!badSize);
        }
    }
}

void CreateBlankDiskDialog::on_sizeBox_currentIndexChanged(int index)
{
    int size = ui->sizeBox->itemData(index).toInt();

    // If it's custom, enable the size spinners
    bool custom = size == 0;
    ui->customSizeBytesSpinner->setEnabled(custom);
    ui->customSizeMBSpinner->setEnabled(custom);

    // If it's not custom, update the spinners
    if (!custom)
    {
        ui->customSizeBytesSpinner->setValue(size);
        // The MB spinner will be updated by this function call
    }
}

void CreateBlankDiskDialog::on_customSizeBytesSpinner_valueChanged(int value)
{
    ui->customSizeMBSpinner->blockSignals(true);
    ui->customSizeMBSpinner->setValue(static_cast<float>(value) / 1048576.0);
    ui->customSizeMBSpinner->blockSignals(false);
    updateFixSectionVisibility();
}

void CreateBlankDiskDialog::on_customSizeMBSpinner_valueChanged(double value)
{
    // Truncate it to 512 after converting to bytes
    int sizeBytes = truncateToNearest512(qRound(value * 1048576.0));

    // Update the other spinner
    ui->customSizeBytesSpinner->blockSignals(true);
    ui->customSizeBytesSpinner->setValue(sizeBytes);
    ui->customSizeBytesSpinner->blockSignals(false);
}

void CreateBlankDiskDialog::on_fixButton_clicked()
{
    ui->customSizeBytesSpinner->setValue(truncateToNearest512(ui->customSizeBytesSpinner->value()));
    updateFixSectionVisibility();
}
