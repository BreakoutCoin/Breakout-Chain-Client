#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionfilterproxy.h"
#include "guiutil.h"
#include "guiconstants.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QPixmap>

#define DECORATION_SIZE 64
#define NUM_ITEMS 8

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), units(N_COLORS, BitcoinUnits::BTC)
    {
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        QString ticker = index.data(TransactionTableModel::TickerRole).toString();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        int nColor = BREAKOUT_COLOR_NONE;
        if (!GetColorFromTicker(ticker.toUtf8().constData(), nColor))
        {
            foreground = COLOR_INVALID;
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(units[nColor], amount, nColor, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    std::vector<int> units;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    vCardLabels(),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);

    vCardLabels.push_back(ui->labelCard00);
    vCardLabels.push_back(ui->labelCard01);
    vCardLabels.push_back(ui->labelCard02);
    vCardLabels.push_back(ui->labelCard03);
    vCardLabels.push_back(ui->labelCard04);
    vCardLabels.push_back(ui->labelCard05);
    vCardLabels.push_back(ui->labelCard06);
    vCardLabels.push_back(ui->labelCard07);
    vCardLabels.push_back(ui->labelCard08);
    vCardLabels.push_back(ui->labelCard09);
    // vCardLabels.push_back(ui->labelCard10);
    // vCardLabels.push_back(ui->labelCard11);
    // vCardLabels.push_back(ui->labelCard12);
    // vCardLabels.push_back(ui->labelCard13);
    // vCardLabels.push_back(ui->labelCard14);

    // clear any text and pixmaps
    for (int i = 0; i < (int) vCardLabels.size(); ++i)
    {
       vCardLabels[i]->setText(QString(""));
       vCardLabels[i]->setPixmap(QPixmap());
    }

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

// Maturity: applies to coinbase (mint)
// Confirmed: 1 or more depth in chain
// Stake: applies to stake
// maps are color:amount
// Deck balances are 0 or 1, so vCards is a sorted vector of cards held.
void OverviewPage::setBalances(const std::map<int, qint64> &mapBalance,
                               const std::map<int, qint64> &mapStake,
                               const std::map<int, qint64> &mapUnconfirmedBalance,
                               const std::map<int, qint64> &mapImmatureBalance,
                               const std::vector<int> &vCards)
{
    int unitBrostake = model->getOptionsModel()->getDisplayUnitBrostake();
    int unitBrocoin = model->getOptionsModel()->getDisplayUnitBrocoin();
    int unitSistercoin = BitcoinUnits::BTC;
    std::map<int, qint64> mapSpendable;
    model->FillNets(mapUnconfirmedBalance, mapBalance, mapSpendable);
    mapCurrentBalance = mapBalance;
    mapCurrentStake = mapStake;
    mapCurrentUnconfirmedBalance = mapUnconfirmedBalance;
    mapCurrentImmatureBalance = mapImmatureBalance;
    vCurrentCards = vCards;

    // TODO: refactor this mess

    // Brostake is only premined, so don't bother with maturity.
    ui->labelBalanceBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapSpendable[BREAKOUT_COLOR_BROSTAKE], BREAKOUT_COLOR_BROSTAKE));
    ui->labelStakeBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapCurrentStake[BREAKOUT_COLOR_BROSTAKE], BREAKOUT_COLOR_BROSTAKE));
    ui->labelUnconfirmedBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapCurrentUnconfirmedBalance[BREAKOUT_COLOR_BROSTAKE], BREAKOUT_COLOR_BROSTAKE));
    ui->labelTotalBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapSpendable[BREAKOUT_COLOR_BROSTAKE] +
                           mapCurrentStake[BREAKOUT_COLOR_BROSTAKE] +
                           mapCurrentUnconfirmedBalance[BREAKOUT_COLOR_BROSTAKE], BREAKOUT_COLOR_BROSTAKE));

    // Brocoin is never stake.
    ui->labelBalanceBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapSpendable[BREAKOUT_COLOR_BROCOIN], BREAKOUT_COLOR_BROCOIN));
    ui->labelUnconfirmedBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapCurrentUnconfirmedBalance[BREAKOUT_COLOR_BROCOIN], BREAKOUT_COLOR_BROCOIN));
    ui->labelImmatureBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapCurrentImmatureBalance[BREAKOUT_COLOR_BROCOIN], BREAKOUT_COLOR_BROCOIN));
    ui->labelTotalBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapSpendable[BREAKOUT_COLOR_BROCOIN] +
                          mapCurrentUnconfirmedBalance[BREAKOUT_COLOR_BROCOIN] +
                          mapCurrentImmatureBalance[BREAKOUT_COLOR_BROCOIN], BREAKOUT_COLOR_BROCOIN));

    // Sistercoin is only mined, never stake.
    ui->labelBalanceSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapSpendable[BREAKOUT_COLOR_SISCOIN], BREAKOUT_COLOR_SISCOIN));
    ui->labelUnconfirmedSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapCurrentUnconfirmedBalance[BREAKOUT_COLOR_SISCOIN], BREAKOUT_COLOR_SISCOIN));
    ui->labelImmatureSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapCurrentImmatureBalance[BREAKOUT_COLOR_SISCOIN], BREAKOUT_COLOR_SISCOIN));
    ui->labelTotalSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapSpendable[BREAKOUT_COLOR_SISCOIN] +
                         mapCurrentUnconfirmedBalance[BREAKOUT_COLOR_SISCOIN], BREAKOUT_COLOR_SISCOIN));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmatureBrocoin = mapCurrentImmatureBalance[BREAKOUT_COLOR_BROCOIN] != 0;
    ui->labelImmatureBrocoin->setVisible(showImmatureBrocoin);
    ui->lblBroImm->setVisible(showImmatureBrocoin);

    // only show stake balance if it's non-zero, so as not to complicate things
    // for the non-staking users
    bool showStakeBrostake = mapCurrentStake[BREAKOUT_COLOR_BROSTAKE] != 0;
    ui->labelStakeBrostake->setVisible(showStakeBrostake);
    ui->lblBrxStake->setVisible(showStakeBrostake);

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmatureSistercoin = mapCurrentImmatureBalance[BREAKOUT_COLOR_SISCOIN] != 0;
    ui->labelImmatureSistercoin->setVisible(showImmatureSistercoin);
    ui->lblSisImm->setVisible(showImmatureSistercoin);


    for (int i = 0; i < (int) vCardLabels.size(); ++i)
    {
        QPixmap cardPixmap;
        if (i < (int) vCards.size())
        {
           setCardPixmap(vCards[i], cardPixmap);
        }
        vCardLabels[i]->setPixmap(cardPixmap);
    }
}

void OverviewPage::setModel(WalletModel *model)
{
    this->model = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        std::map<int, qint64> mapBalance, mapStake,
                              mapUnconfirmedBalance, mapImmatureBalance;
        std::vector<int> vCards;
        std::vector<int64_t> vBalance(0, N_COLORS);
        model->getBalance(GUI_OVERVIEW_COLORS, mapBalance);
        model->getStake(GUI_OVERVIEW_COLORS, mapStake);
        model->getUnconfirmedBalance(GUI_OVERVIEW_COLORS, mapUnconfirmedBalance);
        model->getImmatureBalance(GUI_OVERVIEW_COLORS, mapImmatureBalance);
        model->getHand(vCards);
        setBalances(mapBalance, mapStake, mapUnconfirmedBalance, mapImmatureBalance, vCards);
        connect(model, SIGNAL(balanceChanged(const std::map<int, qint64>&, const std::map<int, qint64>&,
                                             const std::map<int, qint64>&, const std::map<int, qint64>&,
                                             const std::vector<int>)),
                this, SLOT(setBalances(const std::map<int, qint64>&, const std::map<int, qint64>&,
                                       const std::map<int, qint64>&, const std::map<int, qint64>&,
                                       const std::vector<int>&)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrostake(int)),
                                                      this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrocoin(int)),
                                                      this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        if(!mapCurrentBalance.empty())
        {
            std::map<int, qint64> mapStake;
            model->getStake(GUI_OVERVIEW_COLORS, mapStake);
            setBalances(mapCurrentBalance, mapStake,
                        mapCurrentUnconfirmedBalance, mapCurrentImmatureBalance,
                        vCurrentCards);
        }

        // Update txdelegate->units with the current units
        for (int i = 1; i < N_COLORS; ++i)
        {
           txdelegate->units[i] = model->getOptionsModel()->getDisplayUnit(i);
        }

        ui->listTransactions->update();
    }
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

bool OverviewPage::setCardPixmap(int nColor, QPixmap &cardPixmap)
{
    if (!IsDeck(nColor))
    {
       return false;
    }

    cardPixmap = QPixmap(QString(":/cards/%1").arg(COLOR_TICKER[nColor]));
    return true;
}

