#include "editaddressdialog.h"
#include "ui_editaddressdialog.h"
#include "addresstablemodel.h"
#include "guiutil.h"
#include "main.h"
#include "init.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

EditAddressDialog::EditAddressDialog(Mode mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditAddressDialog), mapper(0), mode(mode), model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->addressEdit, this);

    // selects the transaction currency
    ui->currencySelector->addItem(COLOR_NAME[BREAKOUT_COLOR_BROSTAKE],
                                  (int) BREAKOUT_COLOR_BROSTAKE);
    ui->currencySelector->addItem(COLOR_NAME[BREAKOUT_COLOR_BROCOIN],
                                  (int) BREAKOUT_COLOR_BROCOIN);
    ui->currencySelector->addItem(COLOR_NAME[BREAKOUT_COLOR_SISCOIN],
                                  (int) BREAKOUT_COLOR_SISCOIN);
    ui->currencySelector->setCurrentIndex(
                ui->currencySelector->findData(nDefaultCurrency));

    switch(mode)
    {
    case NewReceivingAddress:
        setWindowTitle(tr("New receiving address"));
        ui->currencySelector->setEnabled(true);
        ui->currencySelector->setVisible(true);
        ui->currencyTextLabel->setVisible(false);
        ui->currencyLineEdit->setVisible(false);
        ui->currencyLineEdit->setEnabled(false);
        ui->addressLabel->setVisible(false);
        ui->addressEdit->setEnabled(false);
        ui->addressEdit->setVisible(false);
        ui->stealthCB->setEnabled(true);
        ui->stealthCB->setVisible(true);
        break;
    case NewSendingAddress:
        setWindowTitle(tr("New sending address"));
        ui->currencyLabel->setVisible(false);
        ui->currencySelector->setEnabled(false);
        ui->currencySelector->setVisible(false);
        ui->currencyTextLabel->setVisible(false);
        ui->currencyLineEdit->setVisible(false);
        ui->currencyLineEdit->setEnabled(false);
        ui->stealthCB->setVisible(false);
        break;
    case EditReceivingAddress:
        setWindowTitle(tr("Edit receiving address"));
        ui->currencyLabel->setVisible(false);
        ui->currencySelector->setEnabled(false);
        ui->currencySelector->setVisible(false);
        ui->currencyTextLabel->setVisible(true);
        ui->currencyLineEdit->setVisible(true);
        ui->currencyLineEdit->setEnabled(false);
        ui->addressEdit->setEnabled(false);
        ui->addressEdit->setVisible(true);
        ui->stealthCB->setEnabled(false);
        ui->stealthCB->setVisible(false);
        break;
    case EditSendingAddress:
        setWindowTitle(tr("Edit sending address"));
        ui->currencyLabel->setVisible(false);
        ui->currencySelector->setEnabled(false);
        ui->currencySelector->setVisible(false);
        ui->currencyTextLabel->setVisible(true);
        ui->currencyLineEdit->setVisible(true);
        ui->currencyLineEdit->setEnabled(false);
        ui->stealthCB->setVisible(false);
        break;
    }

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
}

EditAddressDialog::~EditAddressDialog()
{
    delete ui;
}

void EditAddressDialog::setModel(AddressTableModel *model)
{
    this->model = model;
    if(!model)
        return;

    mapper->setModel(model);
    mapper->addMapping(ui->labelEdit, AddressTableModel::Label);
    mapper->addMapping(ui->currencyLineEdit, AddressTableModel::Ticker);
    mapper->addMapping(ui->addressEdit, AddressTableModel::Address);
    mapper->addMapping(ui->stealthCB, AddressTableModel::Type);
}

void EditAddressDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);
}

bool EditAddressDialog::saveCurrentRow()
{
    if(!model)
        return false;

    switch(mode)
    {
    case NewReceivingAddress:
    case NewSendingAddress:
    {
        int typeInd = ui->stealthCB->isChecked() ? AddressTableModel::AT_Stealth : AddressTableModel::AT_Normal;
        address = model->addRow(
                mode == NewSendingAddress ? AddressTableModel::Send : AddressTableModel::Receive,
                ui->currencySelector->itemData(ui->currencySelector->currentIndex()).toInt(),
                ui->labelEdit->text(),
                ui->addressEdit->text(),
                typeInd);
    }
        break;
    case EditReceivingAddress:
    case EditSendingAddress:
        if(mapper->submit())
        {
            address = ui->addressEdit->text();
        }
        break;
    }
    return !address.isEmpty();
}

void EditAddressDialog::accept()
{
    if(!model)
        return;

    if(!saveCurrentRow())
    {
        switch(model->getEditStatus())
        {
        case AddressTableModel::OK:
            QMessageBox::warning(this, windowTitle(),
                tr("Failed for unknown reason."),
                QMessageBox::Ok, QMessageBox::Ok);
            // Failed with unknown reason. Just reject.
            break;
        case AddressTableModel::NO_CHANGES:
            QMessageBox::warning(this, windowTitle(),
                tr("No changes."),
                QMessageBox::Ok, QMessageBox::Ok);
            // No changes were made during edit operation. Just reject.
            break;
        case AddressTableModel::INVALID_ADDRESS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered address \"%1\" is not a valid breakout address.").arg(ui->addressEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AddressTableModel::DUPLICATE_ADDRESS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered address \"%1\" is already in the address book.").arg(ui->addressEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AddressTableModel::WALLET_UNLOCK_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("Could not unlock wallet."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case AddressTableModel::KEY_GENERATION_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("New key generation failed."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        }
        return;
    }
    QDialog::accept();
}

QString EditAddressDialog::getAddress() const
{
    return address;
}

void EditAddressDialog::setAddress(const QString &address)
{
    this->address = address;
    ui->addressEdit->setText(address);
    ui->currencySelector->setCurrentIndex(
                ui->currencySelector->findData(
                        model->colorForAddress(address)));
}
