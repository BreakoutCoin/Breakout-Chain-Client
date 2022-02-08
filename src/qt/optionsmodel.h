#ifndef OPTIONSMODEL_H
#define OPTIONSMODEL_H

#include <QAbstractListModel>

/** Interface from Qt to configuration data structure for Bitcoin client.
   To Qt, the options are presented as a list with the different options
   laid out vertically.
   This can be changed to a tree once the settings become sufficiently
   complex.
 */
class OptionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit OptionsModel(QObject *parent = 0);

    enum OptionID {
        StartAtStartup,         // bool
        MinimizeToTray,         // bool
        MapPortUPnP,            // bool
        MinimizeOnClose,        // bool
        ProxyUse,               // bool
        ProxyIP,                // QString
        ProxyPort,              // int
        ProxySocksVersion,      // int
        FeeBrostake,            // qint64
        FeeBrocoin,             // qint64
        ReserveBalance,         // qint64
        DefaultColor,           // int
        DisplayUnitBrostake,    // BitcoinUnits::Unit
        DisplayUnitBrocoin,     // BitcoinUnits::Unit
        DisplayAddresses,       // bool
        DisplayGenerated,       // bool
        DetachDatabases,        // bool
        Language,               // QString
        CoinControlFeatures,    // bool
        OptionIDRowCount,
    };

    void Init();

    /* Migrate settings from wallet.dat after app initialization */
    bool Upgrade(); /* returns true if settings upgraded */

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

    /* Explicit getters */
    qint64 getTransactionFeeBrostake();
    qint64 getTransactionFeeBrocoin();
    qint64 getReserveBalance();
    bool getMinimizeToTray();
    bool getMinimizeOnClose();
    int getDefaultColor();
    int getDisplayUnit(int nColor);
    int getDisplayUnitBrostake();
    int getDisplayUnitBrocoin();
    bool getDisplayAddresses();
    bool getDisplayGenerated();
    bool getCoinControlFeatures();
    QString getLanguage() { return language; }

private:
    int nDisplayUnitBrostake;
    int nDisplayUnitBrocoin;
    bool bDisplayAddresses;
    bool bDisplayGenerated;
    bool fMinimizeToTray;
    bool fMinimizeOnClose;
    bool fCoinControlFeatures;
    QString language;

signals:
    void defaultColorChanged(int);
    void displayUnitChangedBrostake(int);
    void displayUnitChangedBrocoin(int);
    void transactionFeeChangedBrostake(qint64);
    void transactionFeeChangedBrocoin(qint64);
    void reserveBalanceChanged(qint64);
    void coinControlFeaturesChanged(bool);
};

#endif // OPTIONSMODEL_H
