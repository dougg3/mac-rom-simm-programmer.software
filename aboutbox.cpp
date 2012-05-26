/*
 * Copyright (C) 2011-2012 Doug Brown
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "aboutbox.h"
#include "ui_aboutbox.h"
#include "version.h"

AboutBox *AboutBox::instance()
{
    // Singleton about box
    static AboutBox *_instance = NULL;
    if (!_instance)
    {
        _instance = new AboutBox();
    }
    return _instance;
}

AboutBox::AboutBox(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutBox)
{
    ui->setupUi(this);

    ui->versionLabel->setText("Version " VERSION_STRING);
}

AboutBox::~AboutBox()
{
    delete ui;
}

void AboutBox::on_buttonBox_accepted()
{
    this->close();
}
