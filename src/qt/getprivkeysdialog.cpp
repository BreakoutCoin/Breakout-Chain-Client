#include <QMessageBox>

#include "getprivkeysdialog.h"
#include "ui_getprivkeysdialog.h"

#include "bitcoinunits.h"
#include "optionsmodel.h"

GetPrivkeysDialog::GetPrivkeysDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GetPrivkeysDialog)
{
    ui->setupUi(this);
}

bool GetPrivkeysDialog::setModel(const mapSecretByAddressByColor_t &mapAddrs,
                                 WalletModel *model)
{

    WalletModel::EncryptionStatus status = model->getEncryptionStatus();
    if (status == WalletModel::Locked)
    {
        QMessageBox::warning(this, tr("Wallet Locked"),
            tr("Wallet is locked.\n"
               "Please unlock wallet to display privkeys."),
            QMessageBox::Ok, QMessageBox::Ok);
        return false;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());

    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        return false;
    }

    ui->tableWidget->setColumnWidth(0, 125);
    ui->tableWidget->setColumnWidth(1, 60);
    ui->tableWidget->setColumnWidth(2, 50);

    mapSecretByAddressByColor_t::const_iterator it;
    for (it = mapAddrs.begin(); it != mapAddrs.end(); ++it)
    {
        int nColor = it->first;
        int nDisplayUnit = model->getOptionsModel()->getDisplayUnit(nColor);
        QString strTicker(COLOR_TICKER[nColor]);
        const mapSecretByAddress_t &mapSecrets = it->second;
        mapSecretByAddress_t::const_iterator sit;
        for (sit = mapSecrets.begin(); sit != mapSecrets.end(); ++sit)
        {
            QString strAddr(sit->first.ToString().c_str());
            QString strSecret(sit->second.first.ToString().c_str());
            int64_t nAmt = sit->second.second;
            // QString strAmt = BitcoinUnits::formatWithUnit(nDisplayUnit, nAmt, nColor);
            QString strAmt = BitcoinUnits::format(nDisplayUnit, nAmt, nColor);
            int nRow = ui->tableWidget->rowCount();
            ui->tableWidget->insertRow(nRow);
            ui->tableWidget->setItem(nRow, 0, new QTableWidgetItem(strAmt));
            ui->tableWidget->setItem(nRow, 1, new QTableWidgetItem(strTicker));
            ui->tableWidget->setItem(nRow, 2, new QTableWidgetItem(strSecret));
            ui->tableWidget->setItem(nRow, 3, new QTableWidgetItem(strAddr));
        }
    }
    return true;
}

GetPrivkeysDialog::~GetPrivkeysDialog()
{
    delete ui;
}

void GetPrivkeysDialog::on_buttonBox_accepted()
{
    close();
}
