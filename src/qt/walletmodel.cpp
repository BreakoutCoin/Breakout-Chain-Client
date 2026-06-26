#include "walletmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"

#include "ui_interface.h"
#include "wallet.h"
#include "walletdb.h" // for BackupWallet
#include "base58.h"

#include <QSet>
#include <QTimer>

#if BOOST_VERSION >= 106500
using namespace boost::placeholders;
#endif

WalletModel::WalletModel(CWallet* wallet,
                         OptionsModel* optionsModel,
                         QObject* parent) :
                                    QObject(parent),
                                    wallet(wallet),
                                    optionsModel(optionsModel),
                                    addressTableModel(0),
                                    transactionTableModel(0),
                                    cachedNumTransactions(0),
                                    cachedEncryptionStatus(Unencrypted),
                                    cachedNumBlocks(0)
{
    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(wallet, this);

    nCurrentSendColor = optionsModel->getDefaultColor();

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

bool WalletModel::getPrivateKeys(mapSecretByAddressByColor_t &mapAddrs) const
{
    std::set<int> setColors;
    for (int i = 1; i < (int) BREAKOUT_COLOR_END; ++i)
    {
       setColors.insert(i);
    }
    bool fMultiSig = false;
    wallet->GetPrivateKeys(setColors, fMultiSig, mapAddrs);
    return mapAddrs.size() > 0;
}

// TODO: refactor next 4 methods
bool WalletModel::getConfirmed(const std::vector<int> &vColors,
                               ColorsMap &mapConfirmed) const
{
    mapConfirmed.Clear();
    bool result = false;
    for (int nColor : vColors)
    {
         if (CheckColor(nColor))
         {
             mapConfirmed.Set(nColor, wallet->GetConfirmed(nColor));
             if (!result)
             {
                result = true;
             }
         }
    }
    return result;
}

bool WalletModel::getImmatureStake(const std::vector<int> &vColors,
                                   ColorsMap &mapStake) const
{
    mapStake.Clear();
    bool result = false;
    std::vector<int>::const_iterator it;
    for (it = vColors.begin(); it != vColors.end(); ++it)
    {
         int nColor = (int) *it;
         if (CheckColor(nColor))
         {
             mapStake.Set(nColor, wallet->GetStake(nColor));
             if (!result)
             {
                  result = true;
             }
         }
    }
    return result;
}

bool WalletModel::getImmatureCoinbase(const std::vector<int> &vColors,
                                      ColorsMap &mapCoinbase) const
{
    mapCoinbase.Clear();
    bool result = false;
    std::vector<int>::const_iterator it;
    for (it = vColors.begin(); it != vColors.end(); ++it)
    {
         int nColor = (int) *it;
         if (CheckColor(nColor))
         {
             mapCoinbase.Set(nColor, wallet->GetCoinbase(nColor));
             if (!result)
             {
                  result = true;
             }
         }
    }
    return result;
}

bool WalletModel::getUnconfirmedReceived(const std::vector<int> &vColors,
                                         ColorsMap &mapReceived) const
{
    mapReceived.Clear();
    bool result = false;
    std::vector<int>::const_iterator it;
    for (it = vColors.begin(); it != vColors.end(); ++it)
    {
         int nColor = (int) *it;
         if (CheckColor(nColor))
         {
             mapReceived.Set(nColor, wallet->GetReceived(nColor));
             if (!result)
             {
                  result = true;
             }
         }
    }
    return result;
}

bool WalletModel::getUnconfirmedSent(const std::vector<int> &vColors,
                                     ColorsMap &mapSent) const
{
    mapSent.Clear();
    bool result = false;
    std::vector<int>::const_iterator it;
    for (it = vColors.begin(); it != vColors.end(); ++it)
    {
         int nColor = (int) *it;
         if (CheckColor(nColor))
         {
             mapSent.Set(nColor, wallet->GetSent(nColor));
             if (!result)
             {
                  result = true;
             }
         }
    }
    return result;
}

bool WalletModel::getHand(std::vector<int>& vCards,
                          std::vector<int>& vCardsImmature) const
{
    vCards.clear();
    vCardsImmature.clear();
    std::vector<int> vCardsUnconfirmedUnused;
    wallet->GetHand(1, vCards, vCardsImmature, vCardsUnconfirmedUnused);
    return ((vCards.size() + vCardsImmature.size()) > 0 );
}

void WalletModel::getSnapshot(const std::vector<int>& vColors,
                              ColorsMap& mapConfirmed,
                              ColorsMap& mapStake,
                              ColorsMap& mapCoinbase,
                              ColorsMap& mapReceived,
                              ColorsMap& mapSent,
                              std::vector<int> &vCards) const
{
    getConfirmed(vColors, mapConfirmed);
    getImmatureStake(vColors, mapStake);
    getImmatureCoinbase(vColors, mapCoinbase);
    getUnconfirmedReceived(vColors, mapReceived);
    getUnconfirmedSent(vColors, mapSent);
    std::vector<int> vCardsImmature;
    getHand(vCards, vCardsImmature);
    for (int card : vCardsImmature)
    {
        if (std::find(vCards.begin(), vCards.end(), card) == vCards.end())
        {
            vCards.push_back(card);
        }
    }
    std::sort(vCards.begin(), vCards.end(), cardSorter);
}

int64_t WalletModel::getSpendable(const int nColor) const
{
     return wallet->GetSpendable(nColor);
}

int WalletModel::getNumTransactions() const
{
    int numTransactions = 0;
    {
        LOCK(wallet->cs_wallet);
        numTransactions = wallet->mapWallet.size();
    }
    return numTransactions;
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus)
        emit encryptionStatusChanged(newEncryptionStatus);
}

void WalletModel::pollBalanceChanged()
{
    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet)
        return;

    if(nBestHeight != cachedNumBlocks)
    {
        // Balance and number of transactions might have changed
        cachedNumBlocks = nBestHeight;

        checkBalanceChanged();
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged()
{
    ColorsMap mapNewConfirmed, mapNewStake, mapNewCoinbase,
              mapNewReceived, mapNewSent;
    std::vector<int> vNewCards;

    getConfirmed(GUI_OVERVIEW_COLORS, mapNewConfirmed);
    getImmatureStake(GUI_OVERVIEW_COLORS, mapNewStake);
    getImmatureCoinbase(GUI_OVERVIEW_COLORS, mapNewCoinbase);
    getUnconfirmedReceived(GUI_OVERVIEW_COLORS, mapNewReceived);
    getUnconfirmedSent(GUI_OVERVIEW_COLORS, mapNewSent);
    std::vector<int> vNewCardsImmature;
    getHand(vNewCards, vNewCardsImmature);
    for (int card : vNewCardsImmature)
    {
        if (std::find(vNewCards.begin(), vNewCards.end(), card) ==
            vNewCards.end())
        {
            vNewCards.push_back(card);
        }
    }
    std::sort(vNewCards.begin(), vNewCards.end(), cardSorter);

    if (mapCachedConfirmed != mapNewConfirmed)
    {
        mapCachedConfirmed = mapNewConfirmed;
        emit balanceChanged(mapNewConfirmed, mapNewStake,
                            mapNewCoinbase, mapNewReceived,
                            mapNewSent, vNewCards);
    }
    if (mapCachedStake != mapNewStake)
    {
        mapCachedStake = mapNewStake;
        emit balanceChanged(mapNewConfirmed, mapNewStake,
                            mapNewCoinbase, mapNewReceived,
                            mapNewSent, vNewCards);
    }
    if (mapCachedCoinbase != mapNewCoinbase)
    {
        mapCachedCoinbase = mapNewCoinbase;
        emit balanceChanged(mapNewConfirmed, mapNewStake,
                            mapNewCoinbase, mapNewReceived,
                            mapNewSent, vNewCards);
    }
    if (mapCachedReceived != mapNewReceived)
    {
        mapCachedReceived = mapNewReceived;
        emit balanceChanged(mapNewConfirmed, mapNewStake,
                            mapNewCoinbase, mapNewReceived,
                            mapNewSent, vNewCards);
    }
    if (mapCachedSent != mapNewSent)
    {
        mapCachedSent = mapNewSent;
        emit balanceChanged(mapNewConfirmed, mapNewStake,
                            mapNewCoinbase, mapNewReceived,
                            mapNewSent, vNewCards);
    }
    if (vCachedCards != vNewCards)
    {
        vCachedCards = vNewCards;
        emit balanceChanged(mapNewConfirmed, mapNewStake,
                            mapNewCoinbase, mapNewReceived,
                            mapNewSent, vNewCards);
    }
}

void WalletModel::updateTransaction(const QString &hash, int status)
{
    if(transactionTableModel)
        transactionTableModel->updateTransaction(hash, status);

    // Balance and number of transactions might have changed
    checkBalanceChanged();

    int newNumTransactions = getNumTransactions();
    if(cachedNumTransactions != newNumTransactions)
    {
        cachedNumTransactions = newNumTransactions;
        emit numTransactionsChanged(newNumTransactions);
    }
}


void WalletModel::updateAddressBook(const QString &address, const QString &label, bool isMine, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, status);
}

void WalletModel::updateCurrentSendColor(int nColor)
{
    if (CheckColor(nColor))
    {
        nCurrentSendColor = nColor;
    }
    emit currentSendColorChanged(nColor);
}

// returns BREAKOUT_COLOR_NONE if not valid, address color if valid
int WalletModel::validateAddress(const QString &address)
{
    std::string sAddr = address.toStdString();

    if (sAddr.length() > 75)
    {
        if (IsStealthAddress(sAddr))
        {
            CStealthAddress sxAddr;
            sxAddr.SetEncoded(sAddr);
            return sxAddr.nColor;
        }
    }
    else
    {
        CBitcoinAddress addressParsed(sAddr);
        if (addressParsed.IsValid())
        {
             return addressParsed.nColor;
        }
    }
    return BREAKOUT_COLOR_NONE;
}

WalletModel::AddKeyStatus WalletModel::addPrivKey(
                             const CKey &ckeySecret, const CPubKey &pubkey,
                             const CKeyID &vchAddress, int nColor,
                             const std::string &strLabel)
{
    LOCK2(cs_main, wallet->cs_wallet);

    wallet->MarkDirty();
    wallet->SetAddressBookName(vchAddress, nColor, strLabel);

    // Don't throw error in case a key is already there
    if (wallet->HaveKey(vchAddress))
    {
        return StatusKeyHad;
    }

    wallet->mapKeyMetadata[vchAddress].nCreateTime = 1;

    if (!wallet->AddKeyPubKey(ckeySecret, pubkey))
    {
        return StatusKeyError;
    }

    // whenever a key is imported, we need to scan the whole chain
    wallet->nTimeFirstKey = 1; // 0 would be considered 'no value'

    wallet->ScanForWalletTransactions(pindexGenesisBlock, true);
    wallet->ReacceptWalletTransactions();

    return StatusKeyAdded;
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(
    const QString& txcomment,
    const QList<SendCoinsRecipient>& recipients,
    unsigned int nServiceTypeID,
    const CCoinControl* coinControl)
{
    qint64 total = 0;
    QSet<QString> setAddress;
    QString hex;

    if(recipients.empty())
    {
        return OK;
    }

    int nColor = BREAKOUT_COLOR_NONE;

    // Pre-check input data for validity
    // rcp needs not have a color field because addresses are colored
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        int addrColor = validateAddress(rcp.address);
        if (addrColor == BREAKOUT_COLOR_NONE)
        {
            return InvalidAddress;
        }
        if (nColor == BREAKOUT_COLOR_NONE)
        {
            nColor = addrColor;
        }
        else if (nColor != addrColor)
        {
            return InconsistentAddress;
        }
        setAddress.insert(rcp.address);

        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }
        total += rcp.amount;
    }

    if(recipients.size() > setAddress.size())
    {
        return DuplicateAddress;
    }

    int64_t nBalance = 0;
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(nColor, vCoins, true, coinControl);

    BOOST_FOREACH(const COutput& out, vCoins)
        nBalance += out.tx->vout[out.i].nValue;

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    int nFeeColor = FEE_COLOR[nColor];

    if (nFeeColor == nColor)
    {
        if ((total + vTransactionFee[nColor]) > nBalance)
        {
            return SendCoinsReturn(AmountWithFeeExceedsBalance, vTransactionFee[nColor]);
        }
    }
    else
    {
        if (vTransactionFee[nFeeColor] > wallet->GetSpendable(nFeeColor))
        {
            return SendCoinsReturn(FeeExceedsFeeBalance, vTransactionFee[nFeeColor]);
        } 
    }

    std::map<int, std::string> mapStealthNarr;

    {
        LOCK2(cs_main, wallet->cs_wallet);

        CWalletTx wtx;

        // Sendmany
        std::vector<std::pair<CScript, int64_t> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            std::string sAddr = rcp.address.toStdString();

            if (rcp.typeInd == AddressTableModel::AT_Stealth)
            {
                CStealthAddress sxAddr;
                if (sxAddr.SetEncoded(sAddr))
                {
                    ec_secret ephem_secret;
                    ec_secret secretShared;
                    ec_point pkSendTo;
                    ec_point ephem_pubkey;


                    if (GenerateRandomSecret(ephem_secret) != 0)
                    {
                        printf("GenerateRandomSecret failed.\n");
                        return Aborted;
                    };

                    if (StealthSecret(ephem_secret, sxAddr.scan_pubkey, sxAddr.spend_pubkey, secretShared, pkSendTo) != 0)
                    {
                        printf("Could not generate receiving public key.\n");
                        return Aborted;
                    };

                    CPubKey cpkTo(pkSendTo);
                    if (!cpkTo.IsValid())
                    {
                        printf("Invalid public key generated.\n");
                        return Aborted;
                    };

                    CKeyID ckidTo = cpkTo.GetID();

                    CBitcoinAddress addrTo(ckidTo, nColor);

                    if (SecretToPublicKey(ephem_secret, ephem_pubkey) != 0)
                    {
                        printf("Could not generate ephem public key.\n");
                        return Aborted;
                    };

                    if (fDebug)
                    {
                        printf("Stealth send to generated pubkey %" PRIszu ": %s\n", pkSendTo.size(), HexStr(pkSendTo).c_str());
                        printf("hash %s\n", addrTo.ToString().c_str());
                        printf("ephem_pubkey %" PRIszu ": %s\n", ephem_pubkey.size(), HexStr(ephem_pubkey).c_str());
                    };

                    CScript scriptPubKey;
                    scriptPubKey.SetDestination(addrTo.Get());
                    vecSend.push_back(make_pair(scriptPubKey, rcp.amount));

                    CScript scriptP = CScript() << OP_RETURN << ephem_pubkey;

                    if (rcp.narration.length() > 0)
                    {
                        std::string sNarr = rcp.narration.toStdString();

                        if (sNarr.length() > 24)
                        {
                            printf("Narration is too long.\n");
                            return NarrationTooLong;
                        };

                        valtype vchNarr;
                        
                        SecMsgCrypter crypter;
                        crypter.SetKey(&secretShared.e[0], &ephem_pubkey[0]);
                        
                        if (!crypter.Encrypt((uint8_t*)&sNarr[0], sNarr.length(), vchNarr))
                        {
                            printf("Narration encryption failed.\n");
                            return Aborted;
                        };
                        
                        if (vchNarr.size() > 48)
                        {
                            printf("Encrypted narration is too long.\n");
                            return Aborted;
                        };
                        
                        if (vchNarr.size() > 0)
                            scriptP = scriptP << OP_RETURN << vchNarr;
                        
                        int pos = vecSend.size()-1;
                        mapStealthNarr[pos] = sNarr;
                    };
                    
                    vecSend.push_back(make_pair(scriptP, 0));
                    
                    continue;
                }; // else drop through to normal
            }
            
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(sAddr).Get());
            vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
            
            if (rcp.narration.length() > 0)
            {
                std::string sNarr = rcp.narration.toStdString();
                
                if (sNarr.length() > 24)
                {
                    printf("Narration is too long.\n");
                    return NarrationTooLong;
                };
                
                std::vector<uint8_t> vNarr(sNarr.c_str(), sNarr.c_str() + sNarr.length());
                std::vector<uint8_t> vNDesc;
                
                vNDesc.resize(2);
                vNDesc[0] = 'n';
                vNDesc[1] = 'p';
                
                CScript scriptN = CScript() << OP_RETURN << vNDesc << OP_RETURN << vNarr;
                
                vecSend.push_back(make_pair(scriptN, 0));
            }
        }


        CReserveKey keyChange(wallet);
        CReserveKey keyFeeChange(wallet);
        int64_t nFeeRequired = 0;
        std::string strTxComment = txcomment.toStdString();

        int nChangePos = -1;
        int nFeeChangePos = -1;
        bool fCreated = wallet->CreateTransaction(
                           vecSend, nColor, wtx, keyChange, keyFeeChange,
                           nFeeRequired, nChangePos, nFeeChangePos,
                           strTxComment, nServiceTypeID, coinControl);

        std::map<int, std::string>::iterator it;
        for (it = mapStealthNarr.begin(); it != mapStealthNarr.end(); ++it)
        {
            int pos = it->first;
            if (nChangePos > -1 && it->first >= nChangePos)
                pos++;

            char key[64];
            if (snprintf(key, sizeof(key), "n_%u", pos) < 1)
            {
                printf("CreateStealthTransaction(): Error creating narration key.");
                continue;
            };
            wtx.mapValue[key] = it->second;
        };

        // in some cases the fee required is less than the minimum fee
        if(!fCreated)
        {
            if (nFeeColor == nColor)
            {
                if ((total + nFeeRequired) > nBalance) // FIXME: could cause collisions in the future
                {
                    return SendCoinsReturn(AmountWithFeeExceedsBalance, nFeeRequired);
                }
                return TransactionCreationFailed;
            }
            else
            {
                if (nFeeRequired > wallet->GetSpendable(nFeeColor))
                {
                    return SendCoinsReturn(FeeExceedsFeeBalance, nFeeRequired);
                }
                return TransactionCreationFailed;
            }
        }
        if(!uiInterface.ThreadSafeAskFee(nFeeRequired, nFeeColor, tr("Sending...").toStdString()))
        {
            return Aborted;
        }
        if(!wallet->CommitTransaction(wtx, &keyChange, &keyFeeChange))
        {
            return TransactionCommitFailed;
        }
        hex = QString::fromStdString(wtx.GetHash().GetHex());
    }

    // Add addresses / update labels that we've sent to to the address book
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        std::string strLabel = rcp.label.toStdString(); {
            LOCK(wallet->cs_wallet);
            if (rcp.typeInd == AddressTableModel::AT_Stealth) {
                  wallet->UpdateStealthAddress(strAddress, strLabel, true);
            } else {
                  std::map<CTxDestination, std::string>::iterator mi =
                                              wallet->mapAddressBook.find(dest);
                  // Check if we have a new address or an updated label
                  if (mi == wallet->mapAddressBook.end() || mi->second != strLabel) {
                       wallet->SetAddressBookName(dest, nColor, strLabel);
                  }
            }
        }
    }

    return SendCoinsReturn(OK, 0, nColor, hex);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if (!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if (wallet->IsLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if (locked)
    {
        // Lock
        return wallet->Lock();
    }
    else
    {
        // Unlock
        return wallet->Unlock(passPhrase, true);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data());
}

// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    OutputDebugStringF("NotifyKeyStoreStatusChanged\n");
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet,
                                     const CTxDestination &address, int nColor,
                                     const std::string &label, bool isMine, ChangeType status)
{

    if (address.type() == typeid(CStealthAddress))
    {
        CStealthAddress sxAddr = boost::get<CStealthAddress>(address);
        sxAddr.nColor = nColor;
        std::string enc = sxAddr.Encoded();
        OutputDebugStringF("NotifyAddressBookChanged %s %s isMine=%i status=%i\n", enc.c_str(), label.c_str(), isMine, status);
        QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(enc)),
                                  Q_ARG(QString, QString::fromStdString(label)),
                                  Q_ARG(bool, isMine),
                                  Q_ARG(int, status));
    } else
    {
    OutputDebugStringF("NotifyAddressBookChanged %s %s isMine=%i status=%i\n",
                                    CBitcoinAddress(address, nColor).ToString().c_str(), label.c_str(), isMine, status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                      Q_ARG(QString, QString::fromStdString(CBitcoinAddress(address, nColor).ToString())),
                      Q_ARG(QString, QString::fromStdString(label)),
                      Q_ARG(bool, isMine),
                      Q_ARG(int, status));
    }
}

static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    OutputDebugStringF("NotifyTransactionChanged %s status=%i\n", hash.GetHex().c_str(), status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}

// static void NotifyBlocksChanged(WalletModel *walletmodel)
// {
// /*
//     if (!IsInitialBlockDownload()) {
//     }
// */
// }

static void NotifyReconcileStarted(WalletModel* walletmodel)
{
    QObject* parent = walletmodel->parent();
    if (parent)
    {
        QMetaObject::invokeMethod(parent,
                                  "reconcileStarted",
                                  Qt::QueuedConnection);
    }
}

static void NotifyReconcileEnded(WalletModel* walletmodel)
{
    QObject* parent = walletmodel->parent();
    if (parent)
    {
        QMetaObject::invokeMethod(parent,
                                  "reconcileEnded",
                                  Qt::QueuedConnection);
    }
}

void WalletModel::subscribeToTransactionSignal()
{
    wallet->NotifyTransactionChanged.connect(
        boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

void WalletModel::unsubscribeFromTransactionSignal()
{
    wallet->NotifyTransactionChanged.disconnect(
        boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(
        boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(
        boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.connect(
        boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    // uiInterface.NotifyBlocksChanged.connect(boost::bind(NotifyBlocksChanged,
    // this));
    wallet->NotifyReconcileStarted.connect(
        boost::bind(NotifyReconcileStarted, this));
    wallet->NotifyReconcileEnded.connect(
        boost::bind(NotifyReconcileEnded, this));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(
        boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(
        boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.disconnect(
        boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    // uiInterface.NotifyBlocksChanged.disconnect(boost::bind(NotifyBlocksChanged,
    // this));
    wallet->NotifyReconcileStarted.disconnect(
        boost::bind(NotifyReconcileStarted, this));
    wallet->NotifyReconcileEnded.disconnect(
        boost::bind(NotifyReconcileEnded, this));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = (getEncryptionStatus() == Locked);
    bool was_mintOnly = ((!was_locked) && fWalletUnlockStakingOnly);
    if (was_locked || was_mintOnly)
    {
        // Request UI to unlock wallet
        emit requireUnlock();
    }

    // If wallet is still locked or mint only, unlock was failed or cancelled
    // Mark context as invalid
    bool valid = (getEncryptionStatus() != Locked);

    return UnlockContext(this, valid, was_locked, was_mintOnly);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet,
                                          bool valid,
                                          bool relock,
                                          bool mint ): wallet(wallet),
                                                      valid(valid),
                                                      relock(relock),
                                                      mint(mint) { }

WalletModel::UnlockContext::~UnlockContext()
{
    if (valid && relock)
    {
         wallet->setWalletLocked(true);
    }
    fWalletUnlockStakingOnly = mint;
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);   
}

// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints,
                                       int nColor, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    BOOST_FOREACH(const COutPoint& outpoint, vOutpoints)
    {
        if (!wallet->mapWallet.count(outpoint.hash))
        {
            continue;
        }
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0)
        {
            continue;
        }
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth);
        if (out.tx->vout[out.i].nColor != nColor)
        {
            continue;
        }
        vOutputs.push_back(out);
    }
}

int WalletModel::getCurrentSendColor()
{
    return nCurrentSendColor;
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address) 
void WalletModel::listCoins(int nColor, std::map<QString, std::vector<COutput> >& mapCoins) const
{
    // no reason why multisigs can't be included by default
    static const bool fMultiSig = true;

    std::vector<COutput> vCoins;
    wallet->AvailableCoins(nColor, vCoins, fMultiSig);

    LOCK2(cs_main, wallet->cs_wallet); // ListLockedCoins, mapWallet
    std::vector<COutPoint> vLockedCoins;

    // add locked coins
    BOOST_FOREACH(const COutPoint& outpoint, vLockedCoins)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth);
        vCoins.push_back(out);
    }

    BOOST_FOREACH(const COutput& out, vCoins)
    {
        COutput cout = out;

        while (wallet->IsChange(cout.tx->vout[cout.i]) &&
               (cout.tx->vin.size() > 0) &&
               (wallet->IsMine(cout.tx->vin[0], fMultiSig) & ISMINE_SIGNABLE))
        {
            if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
            cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0);
        }

        CTxDestination address;
        if(!ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address)) continue;
        mapCoins[CBitcoinAddress(address, nColor).ToString().c_str()].push_back(out);
    }
}

// Available + LockedCoins assigned to each address
void WalletModel::listAddresses(int nColor, std::map<QString, int64_t>& mapAddrs) const
{
    std::map<QString, std::vector<COutput> > mapCoins;
    // not perfectly efficient, but it should do
    this->listCoins(nColor, mapCoins);

    std::map<QString, std::vector<COutput> >::iterator it;
    for(it = mapCoins.begin(); it != mapCoins.end(); ++it)
    {
       BOOST_FOREACH(const COutput &out, it->second)
       {
           COutput cout = out;
           CTxDestination destAddr;
           if(!ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, destAddr))
           {
              // should never fail because it just succeeded
              continue;
           }
           // need to get address because they may be change
           QString qAddr(CBitcoinAddress(destAddr, nColor).ToString().c_str());
           std::map<QString, int64_t>::iterator qit;
           qit = mapAddrs.find(qAddr);
           if (qit == mapAddrs.end())
           {
               mapAddrs[qAddr] = cout.tx->vout[cout.i].nValue;
           }
           else
           {
               qit->second += cout.tx->vout[cout.i].nValue;
           }
       }
    }
}

bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
{
    return false;
}

void WalletModel::lockCoin(COutPoint& output)
{
    return;
}

void WalletModel::unlockCoin(COutPoint& output)
{
    return;
}

void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
{
    return;
}
