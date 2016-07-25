#ifndef FINDADDRESSDIALOG_H
#define FINDADDRESSDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

#include "addresscontrol.h"

namespace Ui {
    class FindAddressDialog;
}
class WalletModel;
class CFindAddress;

class FindAddressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindAddressDialog(QWidget *parent = 0);
    ~FindAddressDialog();

    void setModel(WalletModel *model);

    static CAddressControl *addressControl;

private:
    Ui::FindAddressDialog *ui;
    WalletModel *model;
    int sortColumn;
    Qt::SortOrder sortOrder;

    QMenu *contextMenu;
    QTreeWidgetItem *contextMenuItem;

    QString strPad(QString, int, QString);
    void sortView(int, Qt::SortOrder);
    void updateView();

    enum
    {
        COLUMN_CHECKBOX,
        COLUMN_AMOUNT,
        COLUMN_ADDRESS,
        COLUMN_AMOUNT_INT64,
    };

private slots:
    void showMenu(const QPoint &);
    void copyAmount();
    // void copyLabel();
    void copyAddress();
    void viewItemChanged(QTreeWidgetItem*, int);
    void headerSectionClicked(int);
    void buttonBoxClicked(QAbstractButton*);
};

#endif // FINDADDRESSDIALOG_H
