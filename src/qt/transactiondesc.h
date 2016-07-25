#ifndef TRANSACTIONDESC_H
#define TRANSACTIONDESC_H

#include <QString>
#include <QObject>
#include <string>

#include "bitcoinunits.h"

class CWallet;
class CWalletTx;
class CTxIn;
class CTxOut;

/** Provide a human-readable extended HTML description of a transaction.
 */
class TransactionDesc: public QObject
{
    Q_OBJECT
public:
    static QString FormatValue(int64_t nValue, int nColor, int unit=BitcoinUnits::BTC);
    static QString ValueMapToHTML(const std::map<int, int64_t> &valueMap);
    static QString TxInToHTML(const CTxIn &txin, const CWallet* wallet);
    static QString TxOutToHTML(const CTxOut &txout, const CWallet* wallet);
    static QString toHTML(CWallet *wallet, CWalletTx &wtx);

private:
    TransactionDesc() {}

    static QString FormatTxStatus(const CWalletTx& wtx);
};

#endif // TRANSACTIONDESC_H
