#ifndef WALLETMODEL_H
#define WALLETMODEL_H

#include <QObject>
#include <vector>
#include <map>

#include "allocators.h" /* for SecureString */

#include "colors.h"

#define IMPORT_WALLET 1

#ifdef IMPORT_WALLET
extern int nDefaultCurrency;
#endif

class OptionsModel;
class AddressTableModel;
class TransactionTableModel;
class CWallet;
#ifdef IMPORT_WALLET
class CKey;
#endif
class CKeyID;
class CPubKey;
class COutput;
class COutPoint;
class uint256;
class CCoinControl;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class SendCoinsRecipient
{
public:
    QString address;
    QString label;
    QString narration;
    int typeInd;
    qint64 amount;
    int nColor;
};

/** Interface to Bitcoin wallet from Qt view code. */
class WalletModel : public QObject
{
    Q_OBJECT

public:
    explicit WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent = 0);
    ~WalletModel();

    enum StatusCode // Returned by sendCoins
    {
        OK,
        InvalidAmount,
        InvalidColor,
        InvalidAddress,
        InconsistentAddress,
        AmountExceedsBalance,
        AmountWithFeeExceedsBalance,
        FeeExceedsFeeBalance,
        DuplicateAddress,
        TransactionCreationFailed, // Error returned when wallet is still locked
        TransactionCommitFailed,
        NarrationTooLong,
        Aborted
    };

    enum EncryptionStatus
    {
        Unencrypted,  // !wallet->IsCrypted()
        Locked,       // wallet->IsCrypted() && wallet->IsLocked()
        Unlocked      // wallet->IsCrypted() && !wallet->IsLocked()
    };

#ifdef IMPORT_WALLET
    enum AddKeyStatus
    {
        StatusKeyAdded,        // key was added to wallet successfully
        StatusKeyHad,          // key was already in wallet
        StatusKeyError         // error adding key to wallet
    };
#endif

    OptionsModel *getOptionsModel();
    AddressTableModel *getAddressTableModel();
    TransactionTableModel *getTransactionTableModel();

    bool getBalance(const std::vector<int> &vColors,
                    std::map<int, qint64> &mapBalance) const;
    bool getHand(std::vector<int> &vCards) const;
    bool getUnconfirmedBalance(const std::vector<int> &vColors,
                               std::map<int, qint64> &mapBalance) const;
    bool getStake(const std::vector<int> &vColors,
                  std::map<int, qint64> &mapStake) const;
    bool getImmatureBalance(const std::vector<int> &vColors,
                            std::map<int, qint64> &mapBalance) const;
    int getNumTransactions() const;
    EncryptionStatus getEncryptionStatus() const;

    // Check address for validity
    // Returns BREAKOUT_COLOR_NONE if not valid, address color if valid
    int validateAddress(const QString &address);

#ifdef IMPORT_WALLET
    // returns true if added (not already in wallet)
    AddKeyStatus addPrivKey(const CKey &ckeySecret, const CPubKey &pubkey,
                            const CKeyID &vchAddress, int nColor,
                            const std::string &strLabel);
#endif

    // Return status record for SendCoins, contains error id + information
    struct SendCoinsReturn
    {
        SendCoinsReturn(StatusCode status=Aborted,
                         qint64 fee=0,
                         int nColor=BREAKOUT_COLOR_NONE,
                         QString hex=QString()):
            status(status), fee(fee), nColor(nColor), hex(hex) {}
        StatusCode status;
        qint64 fee; // is used in case status is "AmountWithFeeExceedsBalance"
        int nColor; // is used in case status is "AmountWithFeeExceedsBalance"
        QString hex; // is filled with the transaction hash if status is "OK"
    };

    // Send BRO to a list of recipients
    SendCoinsReturn sendCoins(const QString &txcomment, const QList<SendCoinsRecipient> &recipients, unsigned int nServiceTypeID, const CCoinControl *coinControl=NULL);

    // Wallet encryption
    bool setWalletEncrypted(bool encrypted, const SecureString &passphrase);
    // Passphrase only needed when unlocking
    bool setWalletLocked(bool locked, const SecureString &passPhrase=SecureString());
    bool changePassphrase(const SecureString &oldPass, const SecureString &newPass);
    // Wallet backup
    bool backupWallet(const QString &filename);

    // RAI object for unlocking wallet, returned by requestUnlock()
    class UnlockContext
    {
    public:
        UnlockContext(WalletModel *wallet, bool valid, bool relock);
        ~UnlockContext();

        bool isValid() const { return valid; }

        // Copy operator and constructor transfer the context
        UnlockContext(const UnlockContext& obj) { CopyFrom(obj); }
        UnlockContext& operator=(const UnlockContext& rhs) { CopyFrom(rhs); return *this; }
    private:
        WalletModel *wallet;
        bool valid;
        mutable bool relock; // mutable, as it can be set to false by copying

        void CopyFrom(const UnlockContext& rhs);
    };

    UnlockContext requestUnlock();

    bool getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;
    void getOutputs(const std::vector<COutPoint>& vOutpoints, int nColor,
                                                     std::vector<COutput>& vOutputs);
    int getCurrentSendColor();
    void listCoins(int nColor, std::map<QString, std::vector<COutput> >& mapCoins) const;
    void listAddresses(int nColor, std::map<QString, int64_t>& mapAddrs) const;

    void FillNets(const std::map<int, qint64> &mapDebit,
                  const std::map<int, qint64> &mapCredit,
                  std::map<int, qint64> &mapNet);

    bool isLockedCoin(uint256 hash, unsigned int n) const;
    void lockCoin(COutPoint& output);
    void unlockCoin(COutPoint& output);
    void listLockedCoins(std::vector<COutPoint>& vOutpts);

private:
    CWallet *wallet;

    // Wallet has an options model for wallet-specific options
    // (transaction fee, for example)
    OptionsModel *optionsModel;

    AddressTableModel *addressTableModel;
    TransactionTableModel *transactionTableModel;

    // current send color
    int nCurrentSendColor;

    // Cache some values to be able to detect changes
    std::map<int, qint64> mapCachedBalance;
    std::map<int, qint64> mapCachedStake;
    std::map<int, qint64> mapCachedUnconfirmedBalance;
    std::map<int, qint64> mapCachedImmatureBalance;
    std::vector<int> vCachedCards;
    qint64 cachedNumTransactions;
    EncryptionStatus cachedEncryptionStatus;
    int cachedNumBlocks;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void checkBalanceChanged();


public slots:
    /* Wallet status might have changed */
    void updateStatus();
    /* New transaction, or transaction changed status */
    void updateTransaction(const QString &hash, int status);
    /* New, updated or removed address book entry */
    void updateAddressBook(const QString &address, const QString &label, bool isMine, int status);
    /* Current, immature or unconfirmed balance might have changed - emit 'balanceChanged' if so */
    void pollBalanceChanged();
    /* update the current send color */
    void updateCurrentSendColor(int nColor);

signals:
    // Signal that balance in wallet changed
    void balanceChanged(const std::map<int, qint64>& mapBalance,
                        const std::map<int, qint64>& mapStake,
                        const std::map<int, qint64>& mapUnconfirmedBalance,
                        const std::map<int, qint64>& mapImmatureBalance,
                        const std::vector<int>& vCards);

    // Signal that the current send color changed
    void currentSendColorChanged(int nColor);

    // Number of transactions in wallet changed
    void numTransactionsChanged(int count);

    // Encryption status of wallet changed
    void encryptionStatusChanged(int status);

    // Signal emitted when wallet needs to be unlocked
    // It is valid behaviour for listeners to keep the wallet locked after this signal;
    // this means that the unlocking failed or was cancelled.
    void requireUnlock();

    // Asynchronous error notification
    void error(const QString &title, const QString &message, bool modal);
};


#endif // WALLETMODEL_H
