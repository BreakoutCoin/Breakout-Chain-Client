#include "optionsmodel.h"
#include "bitcoinunits.h"
#include <QSettings>

#include "init.h"
#include "walletdb.h"
#include "guiutil.h"

OptionsModel::OptionsModel(QObject *parent) :
    QAbstractListModel(parent)
{
    Init();
}

bool static ApplyProxySettings()
{
    QSettings settings;
    CService addrProxy(settings.value("addrProxy", "127.0.0.1:9050").toString().toStdString());
    int nSocksVersion(settings.value("nSocksVersion", 5).toInt());
    if (!settings.value("fUseProxy", false).toBool()) {
        addrProxy = CService();
        nSocksVersion = 0;
        return false;
    }
    if (nSocksVersion && !addrProxy.IsValid())
        return false;
    if (!IsLimited(NET_IPV4))
        SetProxy(NET_IPV4, addrProxy, nSocksVersion);
    if (nSocksVersion > 4) {
#ifdef USE_IPV6
        if (!IsLimited(NET_IPV6))
            SetProxy(NET_IPV6, addrProxy, nSocksVersion);
#endif
        SetNameProxy(addrProxy, nSocksVersion);
    }
    return true;
}

void OptionsModel::Init()
{
    QSettings settings;

    nDefaultCurrency = settings.value("nDefaultColor", DEFAULT_COLOR).toInt();
    if (nDefaultCurrency == BREAKOUT_COLOR_NONE)
    {
       nDefaultCurrency = DEFAULT_COLOR;
    }
    nDisplayUnitBrostake = settings.value("nDisplayUnitBrostake", BitcoinUnits::BTC).toInt();
    nDisplayUnitBrocoin = settings.value("nDisplayUnitBrocoin", BitcoinUnits::BTC).toInt();
    bDisplayAddresses = settings.value("bDisplayAddresses", false).toBool();
    bDisplayGenerated = settings.value("bDisplayGenerated", false).toBool();
    fMinimizeToTray = settings.value("fMinimizeToTray", false).toBool();
    fMinimizeOnClose = settings.value("fMinimizeOnClose", false).toBool();
    fCoinControlFeatures = settings.value("fCoinControlFeatures", false).toBool();
    vTransactionFee[BREAKOUT_COLOR_BROSTAKE] = settings.value("nTransactionFeeBrostake").toLongLong();
    vTransactionFee[BREAKOUT_COLOR_BROCOIN] = settings.value("nTransactionFeeBrocoin").toLongLong();
    vReserveBalance[BREAKOUT_COLOR_BROSTAKE] = settings.value("nReserveBalance").toLongLong();
    language = settings.value("language", "").toString();

    // These are shared with core Bitcoin; we want
    // command-line options to override the GUI settings:
    if (settings.contains("fUseUPnP"))
        SoftSetBoolArg("-upnp", settings.value("fUseUPnP").toBool());
    if (settings.contains("addrProxy") && settings.value("fUseProxy").toBool())
        SoftSetArg("-proxy", settings.value("addrProxy").toString().toStdString());
    if (settings.contains("nSocksVersion") && settings.value("fUseProxy").toBool())
        SoftSetArg("-socks", settings.value("nSocksVersion").toString().toStdString());
    if (settings.contains("detachDB"))
        SoftSetBoolArg("-detachdb", settings.value("detachDB").toBool());
    if (!language.isEmpty())
        SoftSetArg("-lang", language.toStdString());
}

bool OptionsModel::Upgrade()
{
    QSettings settings;

    if (settings.contains("bImportFinished"))
        return false; // Already upgraded

    settings.setValue("bImportFinished", true);

    // Move settings from old wallet.dat (if any):
    CWalletDB walletdb(strWalletFileName);

    QList<QString> intOptions;
    intOptions << "nDisplayUnitBrostake" << "nDisplayUnitBrocoin"
               << "nTransactionFeeBrostake" << "nTransactionFeeBrocoin"
               << "nReserveBalance" << "nDefaultColor";
    foreach(QString key, intOptions)
    {
        int value = 0;
        if (walletdb.ReadSetting(key.toStdString(), value))
        {
            settings.setValue(key, value);
            walletdb.EraseSetting(key.toStdString());
        }
    }
    QList<QString> boolOptions;
    boolOptions << "bDisplayAddresses" <<
                   "bDisplayGenerated" <<
                   "fMinimizeToTray" <<
                   "fMinimizeOnClose" <<
                   "fUseProxy" << "fUseUPnP";
    foreach(QString key, boolOptions)
    {
        bool value = false;
        if (walletdb.ReadSetting(key.toStdString(), value))
        {
            settings.setValue(key, value);
            walletdb.EraseSetting(key.toStdString());
        }
    }
    try
    {
        CAddress addrProxyAddress;
        if (walletdb.ReadSetting("addrProxy", addrProxyAddress))
        {
            settings.setValue("addrProxy", addrProxyAddress.ToStringIPPort().c_str());
            walletdb.EraseSetting("addrProxy");
        }
    }
    catch (std::ios_base::failure &e)
    {
        // 0.6.0rc1 saved this as a CService, which causes failure when parsing as a CAddress
        CService addrProxy;
        if (walletdb.ReadSetting("addrProxy", addrProxy))
        {
            settings.setValue("addrProxy", addrProxy.ToStringIPPort().c_str());
            walletdb.EraseSetting("addrProxy");
        }
    }
    ApplyProxySettings();
    Init();

    return true;
}


int OptionsModel::rowCount(const QModelIndex & parent) const
{
    return OptionIDRowCount;
}

QVariant OptionsModel::data(const QModelIndex & index, int role) const
{
    if(role == Qt::EditRole)
    {
        QSettings settings;
        switch(index.row())
        {
        case StartAtStartup:
            return QVariant(GUIUtil::GetStartOnSystemStartup());
        case MinimizeToTray:
            return QVariant(fMinimizeToTray);
        case MapPortUPnP:
            return settings.value("fUseUPnP", GetBoolArg("-upnp", true));
        case MinimizeOnClose:
            return QVariant(fMinimizeOnClose);
        case ProxyUse:
            return settings.value("fUseProxy", false);
        case ProxyIP: {
            proxyType proxy;
            if (GetProxy(NET_IPV4, proxy))
                return QVariant(QString::fromStdString(proxy.first.ToStringIP()));
            else
                return QVariant(QString::fromStdString("127.0.0.1"));
        }
        case ProxyPort: {
            proxyType proxy;
            if (GetProxy(NET_IPV4, proxy))
                return QVariant(proxy.first.GetPort());
            else
                return QVariant(9050);
        }
        case ProxySocksVersion:
            return settings.value("nSocksVersion", 5);
        case FeeBrostake:
            return QVariant((qint64) vTransactionFee[BREAKOUT_COLOR_BROSTAKE]);
        case FeeBrocoin:
            return QVariant((qint64) vTransactionFee[BREAKOUT_COLOR_BROCOIN]);
        case ReserveBalance:
            return QVariant((qint64) vReserveBalance[BREAKOUT_COLOR_BROSTAKE]);
        case DefaultColor:
            return QVariant(nDefaultCurrency);
        case DisplayUnitBrostake:
            return QVariant(nDisplayUnitBrostake);
        case DisplayUnitBrocoin:
            return QVariant(nDisplayUnitBrocoin);
        case DisplayAddresses:
            return QVariant(bDisplayAddresses);
        case DisplayGenerated:
            return QVariant(bDisplayGenerated);
        case DetachDatabases:
            return QVariant(bitdb.GetDetach());
        case Language:
            return settings.value("language", "");
        case CoinControlFeatures:
            return QVariant(fCoinControlFeatures);
        default:
            return QVariant();
        }
    }
    return QVariant();
}

bool OptionsModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
    bool successful = true; /* set to false on parse error */
    if(role == Qt::EditRole)
    {
        QSettings settings;
        switch(index.row())
        {
        case StartAtStartup:
            successful = GUIUtil::SetStartOnSystemStartup(value.toBool());
            break;
        case MinimizeToTray:
            fMinimizeToTray = value.toBool();
            settings.setValue("fMinimizeToTray", fMinimizeToTray);
            break;
        case MapPortUPnP:
            fUseUPnP = value.toBool();
            settings.setValue("fUseUPnP", fUseUPnP);
            MapPort();
            break;
        case MinimizeOnClose:
            fMinimizeOnClose = value.toBool();
            settings.setValue("fMinimizeOnClose", fMinimizeOnClose);
            break;
        case ProxyUse:
            settings.setValue("fUseProxy", value.toBool());
            ApplyProxySettings();
            break;
        case ProxyIP: {
            proxyType proxy;
            proxy.first = CService("127.0.0.1", 9050);
            GetProxy(NET_IPV4, proxy);

            CNetAddr addr(value.toString().toStdString());
            proxy.first.SetIP(addr);
            settings.setValue("addrProxy", proxy.first.ToStringIPPort().c_str());
            successful = ApplyProxySettings();
        }
        break;
        case ProxyPort: {
            proxyType proxy;
            proxy.first = CService("127.0.0.1", 9050);
            GetProxy(NET_IPV4, proxy);

            proxy.first.SetPort(value.toInt());
            settings.setValue("addrProxy", proxy.first.ToStringIPPort().c_str());
            successful = ApplyProxySettings();
        }
        break;
        case ProxySocksVersion: {
            proxyType proxy;
            proxy.second = 5;
            GetProxy(NET_IPV4, proxy);

            proxy.second = value.toInt();
            settings.setValue("nSocksVersion", proxy.second);
            successful = ApplyProxySettings();
        }
        break;
        case FeeBrostake:
            vTransactionFee[BREAKOUT_COLOR_BROSTAKE] = value.toLongLong();
            settings.setValue("nTransactionFeeBrostake",
                                   (qint64) vTransactionFee[BREAKOUT_COLOR_BROSTAKE]);
            emit transactionFeeChangedBrostake(vTransactionFee[BREAKOUT_COLOR_BROSTAKE]);
            break;
        case FeeBrocoin:
            vTransactionFee[BREAKOUT_COLOR_BROCOIN] = value.toLongLong();
            settings.setValue("nTransactionFeeBrocoin",
                                   (qint64) vTransactionFee[BREAKOUT_COLOR_BROCOIN]);
            emit transactionFeeChangedBrostake(vTransactionFee[BREAKOUT_COLOR_BROCOIN]);
            break;
        case ReserveBalance:
            vReserveBalance[BREAKOUT_COLOR_BROSTAKE] = value.toLongLong();
            settings.setValue("nReserveBalance",
                                  (qint64) vReserveBalance[BREAKOUT_COLOR_BROSTAKE]);
            emit reserveBalanceChanged(vReserveBalance[BREAKOUT_COLOR_BROSTAKE]);
            break;
        case DefaultColor:
            nDefaultCurrency = value.toInt();
            settings.setValue("nDefaultColor", nDefaultCurrency);
            emit defaultColorChanged(nDefaultCurrency);
            break;
        case DisplayUnitBrostake:
            nDisplayUnitBrostake = value.toInt();
            settings.setValue("nDisplayUnitBrostake", nDisplayUnitBrostake);
            emit displayUnitChangedBrostake(nDisplayUnitBrostake);
            break;
        case DisplayUnitBrocoin:
            nDisplayUnitBrocoin = value.toInt();
            settings.setValue("nDisplayUnitBrocoin", nDisplayUnitBrocoin);
            emit displayUnitChangedBrocoin(nDisplayUnitBrocoin);
            break;
        case DisplayAddresses:
            bDisplayAddresses = value.toBool();
            settings.setValue("bDisplayAddresses", bDisplayAddresses);
            break;
        case DisplayGenerated:
            bDisplayGenerated = value.toBool();
            settings.setValue("bDisplayGenerated", bDisplayGenerated);
            break;
        case DetachDatabases: {
            bool fDetachDB = value.toBool();
            bitdb.SetDetach(fDetachDB);
            settings.setValue("detachDB", fDetachDB);
            }
            break;
        case Language:
            settings.setValue("language", value);
            break;
        case CoinControlFeatures: {
            fCoinControlFeatures = value.toBool();
            settings.setValue("fCoinControlFeatures", fCoinControlFeatures);
            emit coinControlFeaturesChanged(fCoinControlFeatures);
            }
            break;
        default:
            break;
        }
    }
    emit dataChanged(index, index);

    return successful;
}

// brostake and brocoin are the only currencies that have fees
qint64 OptionsModel::getTransactionFeeBrostake()
{
    return vTransactionFee[BREAKOUT_COLOR_BROSTAKE];
}
qint64 OptionsModel::getTransactionFeeBrocoin()
{
    return vTransactionFee[BREAKOUT_COLOR_BROSTAKE];
}


// Brostake is the only staking currency
qint64 OptionsModel::getReserveBalance()
{
    return vReserveBalance[BREAKOUT_COLOR_BROSTAKE];
}

bool OptionsModel::getCoinControlFeatures()
{
    return fCoinControlFeatures;
}

bool OptionsModel::getMinimizeToTray()
{
    return fMinimizeToTray;
}

bool OptionsModel::getMinimizeOnClose()
{
    return fMinimizeOnClose;
}

int OptionsModel::getDefaultColor()
{
    return nDefaultCurrency;
}

int OptionsModel::getDisplayUnitBrostake()
{
    return nDisplayUnitBrostake;
}
int OptionsModel::getDisplayUnitBrocoin()
{
    return nDisplayUnitBrocoin;
}

int OptionsModel::getDisplayUnit(int nColor)
{
    switch (nColor)
    {
    case BREAKOUT_COLOR_BROSTAKE:
           return getDisplayUnitBrostake();
    case BREAKOUT_COLOR_BROCOIN:
           return getDisplayUnitBrocoin();
    case BREAKOUT_COLOR_SISCOIN:
           return BitcoinUnits::BTC;
    default:
           return BitcoinUnits::BTC;
    }
}

bool OptionsModel::getDisplayAddresses()
{
    return bDisplayAddresses;
}

bool OptionsModel::getDisplayGenerated()
{
    return bDisplayGenerated;
}
