#include "transactionrecord.h"

#include "wallet.h"
#include "base58.h"

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx, bool fShowGenerated)
{
    if (wtx.IsCoinBase() || wtx.IsCoinMint() || wtx.IsCoinStake())
    {
        // Ensures we show generated coins / mined transactions at depth 1
        if (!(fShowGenerated && wtx.IsInMainChain()))
        {
            return false;
        }
    }
    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    // individual tx do not affect any representation
    static const bool fMultiSig = true;

    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    uint256 hash = wtx.GetHash(), hashPrev = 0;
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    int nTxColor = wtx.vout[0].nColor;
    int nFeeColor = FEE_COLOR[nTxColor];

    std::map<int, int64_t> mapDebit, mapCredit, mapChange, mapNet;
    // debits
    wallet->FillDebits(wtx, mapDebit, fMultiSig);
    // credits
    wallet->FillCredits(wtx, mapCredit, fMultiSig);
    // changes
    wallet->FillChange(wtx, mapChange);

    // nets
    FillNets(mapDebit, mapCredit, mapNet);

    char cbuf[256];

    if (ValueMapAllPositive(mapNet) || wtx.IsCoinBase() || wtx.IsCoinStake())
    {
        //
        // Credit
        //
        for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
        {
            const CTxOut& txout = wtx.vout[nOut];

            if (wallet->IsMine(txout, fMultiSig) & ISMINE_ALL)
            {
                TransactionRecord sub(hash, nTime);
                sub.nColor = txout.nColor;
                CTxDestination address;
                sub.idx = parts.size(); // sequence number
                sub.credit = txout.nValue;
                if (ExtractDestination(txout.scriptPubKey, address) && 
                    (IsMine(*wallet, address, fMultiSig) & ISMINE_ALL))
                {
                    // Received by Bitcoin Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = CBitcoinAddress(address, txout.nColor).ToString();
                }
                else
                {
                    // Received by IP connection (deprecated features),
                    //    or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }

                snprintf(cbuf, sizeof(cbuf), "n_%u", nOut);
                mapValue_t::const_iterator mi = wtx.mapValue.find(cbuf);
                if (mi != wtx.mapValue.end() && !mi->second.empty())
                    sub.narration = mi->second;

                if (wtx.IsCoinBase())
                {
                    sub.type = TransactionRecord::Generated;
                    parts.append(sub);
                }
                else if (wtx.IsCoinStake())
                {
                    if (hashPrev == hash)
                        continue; // last coinstake output

                    sub.type = TransactionRecord::Generated;

                    int64_t nDebit = mapDebit[nTxColor];
                    int64_t nNet = mapNet[nTxColor];

                    // Normally this would be 0, but some stakers may want to move
                    //    part or even all of the stake to coinbase.
                    sub.credit = (nNet > 0) ? nNet : wtx.GetValueOut(nTxColor) - nDebit;
                    hashPrev = hash;

                    // Stakers probably do not want to see a bunch of zero credit
                    //    staking transactions cluttering their history.
                    if (sub.credit != 0)
                    {
                         parts.append(sub);
                    }
                }
                else
                {
                    parts.append(sub);
                }
            }
        }
    }
    else
    {
        bool fAllFromMe = true;
        BOOST_FOREACH(const CTxIn& txin, wtx.vin)
        {
            if (wallet->IsMine(txin, fMultiSig) & ISMINE_SPENDABLE)
            {
                continue;
            }
            fAllFromMe = false;
            break;
        };

        bool fAllToMe = true;
        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            opcodetype firstOpCode;
            CScript::const_iterator pc = txout.scriptPubKey.begin();
            if (txout.scriptPubKey.GetOp(pc, firstOpCode)
                && firstOpCode == OP_RETURN)
            {
                continue;
            }
            if (wallet->IsMine(txout, fMultiSig) & ISMINE_SPENDABLE)
            {
                continue;
            }
            
            fAllToMe = false;
            break;
        };

        if (fAllFromMe && fAllToMe)
        {
            // narrations go with the tx currency
            std::string narration("");
            mapValue_t::const_iterator mi;
            for (mi = wtx.mapValue.begin(); mi != wtx.mapValue.end(); ++mi)
            {
                if (mi->first.compare(0, 2, "n_") != 0)
                {
                    continue;
                }
                narration = mi->second;
                break;
            };

            if (nTxColor == nFeeColor)
            {
                int64_t nCredit = mapCredit[nTxColor];
                int64_t nDebit = mapDebit[nTxColor];
                // Payment to self
                int64_t nChange = mapChange[nTxColor];

                parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf,
                             "", narration, -(nDebit - nChange), nCredit - nChange, nTxColor));
            }
            else
            {
                // tx currency
                parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf,
                                  "", narration, -(mapDebit[nTxColor] - mapChange[nTxColor]),
                                  mapCredit[nTxColor] - mapChange[nTxColor], nTxColor));

                // fee currency
                parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf,
                                  "", narration, -(mapDebit[nFeeColor] - mapChange[nFeeColor]),
                                    mapCredit[nFeeColor] - mapChange[nFeeColor], nFeeColor));
            }
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //

            // values out
            std::map<int, int64_t> mapValuesOut;
            wtx.FillValuesOut(mapValuesOut);

            int64_t nTxFee = mapDebit[nFeeColor] - mapValuesOut[nFeeColor];

            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                TransactionRecord sub(hash, nTime);
                sub.nColor = txout.nColor;
                sub.idx = parts.size();

                opcodetype firstOpCode;
                CScript::const_iterator pc = txout.scriptPubKey.begin();
                if (txout.scriptPubKey.GetOp(pc, firstOpCode)
                    && firstOpCode == OP_RETURN)
                    continue;

                if (wallet->IsMine(txout, fMultiSig) & ISMINE_SPENDABLE)
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to Bitcoin Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = CBitcoinAddress(address, txout.nColor).ToString();
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }
                
                snprintf(cbuf, sizeof(cbuf), "n_%u", nOut);
                mapValue_t::const_iterator mi = wtx.mapValue.find(cbuf);
                if (mi != wtx.mapValue.end() && !mi->second.empty())
                    sub.narration = mi->second;

                int64_t nValue = txout.nValue;
                /* Add fee to first output of send color */
                if ((nTxFee > 0 ) && (txout.nColor == nFeeColor))
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }

            // If the fee was not absorbed into any output, account for it explicitly.
            if (nTxFee != 0)
            {
                TransactionRecord sub(hash, nTime);
                sub.nColor = nFeeColor;
                sub.idx = parts.size();
                sub.type = TransactionRecord::DelegatedFee;
                sub.debit = -nTxFee;
                nTxFee = 0;
                parts.append(sub);
            }

        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //
            std::map<int, int64_t>::const_iterator itnet;
            for (itnet = mapNet.begin(); itnet != mapNet.end(); ++itnet)
            {
                if (itnet->second != 0)
                {
                    parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other,
                                            "", "", itnet->second, 0, itnet->first));
                }
            }
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = NULL;
    std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = nBestHeight;

    if (!wtx.IsFinal())
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = nBestHeight - wtx.nLockTime;
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }

    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
}

bool TransactionRecord::statusUpdateNeeded()
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != nBestHeight;
}

std::string TransactionRecord::getTxID()
{
    return hash.ToString() + strprintf("-%03d", idx);
}

