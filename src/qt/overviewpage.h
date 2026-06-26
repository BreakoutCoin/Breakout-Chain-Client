#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>
#include <QLabel>

#include "colors.h"

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

namespace Ui {
    class OverviewPage;
}
class WalletModel;
class TxViewDelegate;
class TransactionFilterProxy;

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget *parent = 0);
    ~OverviewPage();

    void setModel(WalletModel *model);
    void showOutOfSyncWarning(bool fShow);

public slots:
    void setBalances(const ColorsMap &mapConfirmed,
                     const ColorsMap &mapStake,
                     const ColorsMap &mapCoinbase,
                     const ColorsMap &mapReceived,
                     const ColorsMap &mapSent,
                     const std::vector<int>& vCards);

signals:
    void transactionClicked(const QModelIndex &index);

private:
    Ui::OverviewPage *ui;
    WalletModel *model;
    ColorsMap mapCurrentBalance;
    ColorsMap mapCurrentStake;
    ColorsMap mapCurrentUnconfirmedBalance;
    ColorsMap mapCurrentImmatureBalance;
    std::vector<int> vCurrentCards;

    std::vector<QLabel*> vCardLabels;

    TxViewDelegate *txdelegate;
    TransactionFilterProxy *filter;

    bool setCardPixmap(int nColor, QPixmap &cardPixmap);

private slots:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
};

#endif // OVERVIEWPAGE_H
