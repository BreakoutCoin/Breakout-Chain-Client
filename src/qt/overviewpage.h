#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>
#include <QLabel>

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
    void setBalances(const std::map<int, qint64> &mapBalance,
                     const std::map<int, qint64> &mapStake,
                     const std::map<int, qint64> &mapUnconfirmedBalance,
                     const std::map<int, qint64> &mapImmatureBalance,
                     const std::vector<int> &vCards);

signals:
    void transactionClicked(const QModelIndex &index);

private:
    Ui::OverviewPage *ui;
    WalletModel *model;
    std::map<int, qint64> mapCurrentBalance;
    std::map<int, qint64> mapCurrentStake;
    std::map<int, qint64> mapCurrentUnconfirmedBalance;
    std::map<int, qint64> mapCurrentImmatureBalance;
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
