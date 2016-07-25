#include "findaddressdialog.h"
#include "ui_findaddressdialog.h"

#include "init.h"
#include "bitcoinunits.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "optionsmodel.h"
#include "addresscontrol.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

using namespace std;
CAddressControl* FindAddressDialog::addressControl = new CAddressControl();

FindAddressDialog::FindAddressDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FindAddressDialog),
    model(0)
{
    ui->setupUi(this);

    // context menu actions
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);

    // context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyAddressAction);

    // context menu signals
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));

    // click on checkbox: copy to clipboard (needs to be radio)
    connect(ui->treeWidget, SIGNAL(itemChanged( QTreeWidgetItem*, int)), this, SLOT(viewItemChanged( QTreeWidgetItem*, int)));

    // click on header
#if QT_VERSION < 0x050000
    ui->treeWidget->header()->setClickable(true);
#else
    ui->treeWidget->header()->setSectionsClickable(true);
#endif
    connect(ui->treeWidget->header(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    // ok button
    connect(ui->buttonBox, SIGNAL(clicked( QAbstractButton*)), this, SLOT(buttonBoxClicked(QAbstractButton*)));


    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 200);
    ui->treeWidget->setColumnWidth(COLUMN_ADDRESS, 450);

    // store amount int64_t in this column, but dont   show it
    ui->treeWidget->setColumnHidden(COLUMN_AMOUNT_INT64, true);


    // default view is sorted by amount desc
    sortView(COLUMN_AMOUNT_INT64, Qt::DescendingOrder);
}

FindAddressDialog::~FindAddressDialog()
{
    delete ui;
}

void FindAddressDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel() && model->getAddressTableModel())
    {
        updateView();
    }
}

// helper function str_pad
QString FindAddressDialog::strPad(QString s, int nPadLength, QString sPadding)
{
    while (s.length() < nPadLength)
        s = sPadding + s;

    return s;
}

// ok button
void FindAddressDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
        done(QDialog::Accepted); // closes the dialog
}


// context menu: need to enforce list mode!
void FindAddressDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void FindAddressDialog::copyAmount()
{
    QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_AMOUNT));
}

// context menu action: copy address
void FindAddressDialog::copyAddress()
{
        QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_ADDRESS));
}


// treeview: sort
void FindAddressDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn == COLUMN_AMOUNT_INT64 ? COLUMN_AMOUNT : sortColumn, sortOrder);
}

// treeview: clicked on header
void FindAddressDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(COLUMN_AMOUNT_INT64, sortOrder);
    }
    else
    {
        if (logicalIndex == COLUMN_AMOUNT) // sort by amount
            logicalIndex = COLUMN_AMOUNT_INT64;

        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            // if amount then default => desc, else default => asc
            sortOrder = ((sortColumn == COLUMN_AMOUNT_INT64) ? Qt::DescendingOrder : Qt::AscendingOrder);
        }
        sortView(sortColumn, sortOrder);
    }
}


// checkbox clicked by user
void FindAddressDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
        std::string sAddr = item->text(COLUMN_ADDRESS).toStdString();
        std::string sAmt = item->text(COLUMN_AMOUNT).toStdString();

        AddrPair addr(sAddr, sAmt);


        printf("here 0\n");
        addressControl->SetNull();

        if (item == NULL) {
            return;
        }

        // make these work like radio buttons
        QTreeWidgetItemIterator it(ui->treeWidget);
        while (*it)
        {
            if (*it == item)
            {
                 if (item->isDisabled())
	         {
                 printf("here 1\n");
                        item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
                 printf("here 2\n");
	         }
                 else if (item->checkState(COLUMN_CHECKBOX) == Qt::Checked)
                 {
                 printf("here 4\n");
                       addressControl->Select(addr);
                 printf("here 5\n");
                 }
            }
            else
            {
                 printf("here 6\n");
                 (*it)->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
            }
                 printf("here 7\n");
            ++it;
                 printf("here 8\n");
	}
                 printf("here 9\n");
}

void FindAddressDialog::updateView()
{
    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false);
    ui->treeWidget->setAlternatingRowColors(true);
    QFlags<Qt::ItemFlag> flgCheckbox=Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    // QFlags<Qt::ItemFlag> flgTristate=Qt::ItemIsSelectable | Qt::ItemIsEnabled |
    //                                                         Qt::ItemIsUserCheckable | Qt::ItemIsTristate;
    
    int nDisplayUnit = BitcoinUnits::BTC;
    if (model && model->getOptionsModel())
    {
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit(
                 model->getOptionsModel()->getDefaultColor());
    }

    map<QString, int64_t> mapAddrs;
    model->listAddresses(BREAKOUT_COLOR_NONE, mapAddrs);

    BOOST_FOREACH(PAIRTYPE(QString, int64_t) addr, mapAddrs)
    {
        // QTreeWidgetItem *itemWalletAddress = new QTreeWidgetItem();
        QString qsAddress = addr.first;

        // [TODO] keep these for now, may do labels later
        // QString sWalletLabel = "";
        // if (model->getAddressTableModel())
        //     sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
        // if (sWalletLabel.length() == 0)
        //     sWalletLabel = tr("(no label)");
        

        int64_t nValue = addr.second;
            
        QTreeWidgetItem *itemOutput;
        itemOutput = new QTreeWidgetItem(ui->treeWidget);
        itemOutput->setFlags(flgCheckbox);
        itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
                
        itemOutput->setText(COLUMN_ADDRESS, qsAddress);
                    
        // [TODO] labels
        // if (!(sAddress == sWalletAddress)) // change
        // {
        //     // tooltip from where the change comes from
        //     itemOutput->setToolTip(COLUMN_LABEL, tr("change from %1 (%2)").arg(sWalletLabel).arg(sWalletAddress));
        //     itemOutput->setText(COLUMN_LABEL, tr("(change)"));
        // }
        // else
        // {
        //     QString sLabel = "";
        //     if (model->getAddressTableModel())
        //         sLabel = model->getAddressTableModel()->labelForAddress(sAddress);
        //     if (sLabel.length() == 0)
        //         sLabel = tr("(no label)");
        //     itemOutput->setText(COLUMN_LABEL, sLabel); 
        // }

        QString qsAmt(BitcoinUnits::format(nDisplayUnit, nValue, BREAKOUT_COLOR_NONE));

        // amount
        itemOutput->setText(COLUMN_AMOUNT, qsAmt);
        // padding so that sorting works correctly
        itemOutput->setText(COLUMN_AMOUNT_INT64, strPad(QString::number(nValue), 15, " "));

        AddrPair prAddr(qsAddress.toStdString(), qsAmt.toStdString());

        // set checkbox
        if (addressControl->IsSelected(prAddr))
        {
             itemOutput->setCheckState(COLUMN_CHECKBOX,Qt::Checked);
        }

    }
    
    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
}
