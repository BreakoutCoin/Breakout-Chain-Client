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
void OverviewPage::setBalances(const ColorsMap& mapConfirmed,
                               const ColorsMap& mapStake,
                               const ColorsMap& mapCoinbase,
                               const ColorsMap& mapReceived,
                               const ColorsMap& mapSent,
                               const std::vector<int>& vCards)
{
    int unitBrostake = model->getOptionsModel()->getDisplayUnitBrostake();
    int unitBrocoin = model->getOptionsModel()->getDisplayUnitBrocoin();
    int unitSistercoin = BitcoinUnits::BTC;
    // Spendable = Confirmed - Stake - Coinbase - Sent
    ColorsMap mapSpendable = mapConfirmed;
    mapSpendable.Subtract(mapStake);
    mapSpendable.Subtract(mapCoinbase);
    mapSpendable.Subtract(mapSent);
    // Balance = Confirmed - Sent
    mapCurrentBalance = mapConfirmed;
    mapCurrentBalance.Subtract(mapSent);
    mapCurrentStake = mapStake;
    mapCurrentUnconfirmedBalance = mapReceived;
    mapCurrentImmatureBalance = mapCoinbase;
    vCurrentCards = vCards;

    // TODO: refactor this mess

    // Brostake is only premined, so don't bother with maturity.
    ui->labelBalanceBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapSpendable.Get(BREAKOUT_COLOR_BRX), BREAKOUT_COLOR_BRX));
    ui->labelStakeBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapCurrentStake.Get(BREAKOUT_COLOR_BRX), BREAKOUT_COLOR_BRX));
    ui->labelUnconfirmedBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapCurrentUnconfirmedBalance.Get(BREAKOUT_COLOR_BRX), BREAKOUT_COLOR_BRX));
    ui->labelTotalBrostake->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrostake, mapSpendable.Get(BREAKOUT_COLOR_BRX) +
                           mapCurrentStake.Get(BREAKOUT_COLOR_BRX) +
                           mapCurrentUnconfirmedBalance.Get(BREAKOUT_COLOR_BRX), BREAKOUT_COLOR_BRX));

    // Brocoin is never stake.
    ui->labelBalanceBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapSpendable.Get(BREAKOUT_COLOR_BRK), BREAKOUT_COLOR_BRK));
    ui->labelUnconfirmedBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapCurrentUnconfirmedBalance.Get(BREAKOUT_COLOR_BRK), BREAKOUT_COLOR_BRK));
    ui->labelImmatureBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapCurrentImmatureBalance.Get(BREAKOUT_COLOR_BRK), BREAKOUT_COLOR_BRK));
    ui->labelTotalBrocoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitBrocoin, mapSpendable.Get(BREAKOUT_COLOR_BRK) +
                          mapCurrentUnconfirmedBalance.Get(BREAKOUT_COLOR_BRK) +
                          mapCurrentImmatureBalance.Get(BREAKOUT_COLOR_BRK), BREAKOUT_COLOR_BRK));

    // Sistercoin is only mined, never stake.
    ui->labelBalanceSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapSpendable.Get(BREAKOUT_COLOR_SIS), BREAKOUT_COLOR_SIS));
    ui->labelUnconfirmedSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapCurrentUnconfirmedBalance.Get(BREAKOUT_COLOR_SIS), BREAKOUT_COLOR_SIS));
    ui->labelImmatureSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapCurrentImmatureBalance.Get(BREAKOUT_COLOR_SIS), BREAKOUT_COLOR_SIS));
    ui->labelTotalSistercoin->setText(BitcoinUnits::formatWithUnitLocalized(
             unitSistercoin, mapSpendable.Get(BREAKOUT_COLOR_SIS) +
                         mapCurrentUnconfirmedBalance.Get(BREAKOUT_COLOR_SIS), BREAKOUT_COLOR_SIS));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmatureBrocoin = mapCurrentImmatureBalance.Get(BREAKOUT_COLOR_BRK) != 0;
    ui->labelImmatureBrocoin->setVisible(showImmatureBrocoin);
    ui->lblBroImm->setVisible(showImmatureBrocoin);

    // only show stake balance if it's non-zero, so as not to complicate things
    // for the non-staking users
    bool showStakeBrostake = mapCurrentStake.Get(BREAKOUT_COLOR_BRX) != 0;
    ui->labelStakeBrostake->setVisible(showStakeBrostake);
    ui->lblBrxStake->setVisible(showStakeBrostake);

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmatureSistercoin = mapCurrentImmatureBalance.Get(BREAKOUT_COLOR_SIS) != 0;
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
        ColorsMap mapConfirmed, mapStake, mapCoinbase,
                  mapReceived, mapSent;
        std::vector<int> vCards;
        std::vector<int> vCardsImmature;
        std::vector<int64_t> vBalance(0, N_COLORS);
        model->getConfirmed(GUI_OVERVIEW_COLORS, mapConfirmed);
        model->getImmatureStake(GUI_OVERVIEW_COLORS, mapStake);
        model->getImmatureCoinbase(GUI_OVERVIEW_COLORS, mapCoinbase);
        model->getUnconfirmedReceived(GUI_OVERVIEW_COLORS, mapReceived);
        model->getUnconfirmedSent(GUI_OVERVIEW_COLORS, mapSent);
        model->getHand(vCards, vCardsImmature);
        for (int card : vCardsImmature)
        {
            if (std::find(vCards.begin(), vCards.end(), card) == vCards.end())
            {
                vCards.push_back(card);
            }
        }
        std::sort(vCards.begin(), vCards.end(), cardSorter);
        setBalances(mapConfirmed,
                    mapStake,
                    mapCoinbase,
                    mapReceived,
                    mapSent,
                    vCards);
        connect(model,
                SIGNAL(balanceChanged(ColorsMap,
                                      ColorsMap,
                                      ColorsMap,
                                      ColorsMap,
                                      ColorsMap,
                                      std::vector<int>)),
                this,
                SLOT(setBalances(ColorsMap,
                                 ColorsMap,
                                 ColorsMap,
                                 ColorsMap,
                                 ColorsMap,
                                 std::vector<int>)));

        connect(model->getOptionsModel(),
                SIGNAL(displayUnitChangedBrostake(int)),
                this,
                SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(),
                SIGNAL(displayUnitChangedBrocoin(int)),
                this,
                SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        if(!mapCurrentBalance.Empty())
        {
            ColorsMap confirmed, stake, coinbase, received, sent;
            std::vector<int> cards;
            model->getSnapshot(GUI_OVERVIEW_COLORS,
                               confirmed,
                               stake,
                               coinbase,
                               received,
                               sent,
                               cards);
            setBalances(confirmed, stake, coinbase, received, sent, cards);
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

