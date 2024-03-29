#ifndef TRANSACTIONRECORD_H
#define TRANSACTIONRECORD_H

#include "uint256.h"

#include "colors.h"

#include <QList>

class CWallet;
class CWalletTx;

/** UI model for transaction status. The transaction status is the part of a transaction that will change over time.
 */
class TransactionStatus
{
public:
    TransactionStatus():
            countsForBalance(false), sortKey(""),
            matures_in(0), status(Offline), depth(0), open_for(0), cur_num_blocks(-1)
    { }

    enum Status {
        Confirmed,          /**< Have 6 or more confirmations (normal tx) or fully mature (mined tx) **/
        /// Normal (sent/received) transactions
        OpenUntilDate,      /**< Transaction not yet final, waiting for date */
        OpenUntilBlock,     /**< Transaction not yet final, waiting for block */
        Offline,            /**< Not sent to any other nodes **/
        Unconfirmed,        /**< Not yet mined into a block **/
        Confirming,         /**< Confirmed, but waiting for the recommended number of confirmations **/
        Conflicted,         /**< Conflicts with other transaction or mempool **/
        /// Generated (mined) transactions
        Immature,           /**< Mined but waiting for maturity */
        MaturesWarning,     /**< Transaction will likely not mature because no nodes have confirmed */
        NotAccepted         /**< Mined but not accepted */
    };

    /// Transaction counts towards available balance
    bool countsForBalance;
    /// Sorting key based on status
    std::string sortKey;

    /** @name Generated (mined) transactions
       @{*/
    int matures_in;
    /**@}*/

    /** @name Reported status
       @{*/
    Status status;
    int64_t depth;
    int64_t open_for; /**< Timestamp if status==OpenUntilDate, otherwise number of blocks */
    /**@}*/

    /** Current number of blocks (to know whether cached status is still valid) */
    int cur_num_blocks;
};

/** UI model for a transaction. A core transaction can be represented by multiple UI transactions if it has
    multiple outputs.
 */
class TransactionRecord
{
public:
    enum Type
    {
        Other,
        Generated,       // applies to stake because it matures like mint
        SendToAddress,
        SendToOther,
        RecvWithAddress,
        RecvFromOther,
        SendToSelf,
        DelegatedFee
    };

    /** Number of confirmation recommended for accepting a transaction */
    static const int RecommendedNumConfirmations = 6;

    TransactionRecord():
            hash(), time(0), type(Other), address(""), narration(""), debit(0),
                                  credit(0), nColor(BREAKOUT_COLOR_NONE), idx(0)
    {
    }

    TransactionRecord(uint256 hash, int64_t time):
            hash(hash), time(time), type(Other), address(""), narration(""), debit(0),
            credit(0), nColor(BREAKOUT_COLOR_NONE), idx(0)
    {
    }

    TransactionRecord(uint256 hash, int64_t time,
                Type type, const std::string &address, const std::string &narration,
                int64_t debit, int64_t credit, int nColor):
            hash(hash), time(time), type(type), address(address), narration(narration),
                  debit(debit), credit(credit), nColor(nColor), idx(0)
    {
    }

    /** Decompose CWallet transaction to model transaction records.
     */
    static bool showTransaction(const CWalletTx &wtx, bool fShowGenerated);
    static QList<TransactionRecord> decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx);

    /** @name Immutable transaction attributes
      @{*/
    uint256 hash;
    qint64 time;
    Type type;
    std::string address;
    std::string narration;
    qint64 debit;
    qint64 credit;
    int nColor;
    /**@}*/

    /** Subtransaction index, for sort key */
    int idx;

    /** Status: can change with block chain update */
    TransactionStatus status;

    /** Return the unique identifier for this transaction (part) */
    std::string getTxID();

    /** Update status from core wallet tx.
     */
    void updateStatus(const CWalletTx &wtx);

    /** Return whether a status update is needed.
     */
    bool statusUpdateNeeded();
};

#endif // TRANSACTIONRECORD_H
