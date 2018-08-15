#ifndef GETPRIVKEYSDIALOG_H
#define GETPRIVKEYSDIALOG_H

#include <QDialog>

#include "walletmodel.h"

namespace Ui {
    class GetPrivkeysDialog;
}
class ClientModel;

/** "Get Privkeys" dialog box */
class GetPrivkeysDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GetPrivkeysDialog(QWidget *parent = 0);
    ~GetPrivkeysDialog();

    bool setModel(const mapSecretByAddressByColor_t &mapAddrs,
                  WalletModel *model);

private:
    Ui::GetPrivkeysDialog *ui;

private slots:
    void on_buttonBox_accepted();
};

#endif // GETPRIVKEYSDIALOG_H
