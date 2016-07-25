#ifndef BITCOINUNITS_H
#define BITCOINUNITS_H

#include <QString>
#include <QAbstractListModel>

#include "colors.h"

/** Bitcoin unit definitions. Encapsulates parsing and formatting
   and serves as list model for drop-down selection boxes.
*/
class BitcoinUnits: public QAbstractListModel
{
public:
    explicit BitcoinUnits(QObject *parent, int nColorIn);

    /** Bitcoin units.
      @note Source: https://en.bitcoin.it/wiki/Units . Please add only sensible ones
     */
    enum Unit
    {
        BTC=0,
        mBTC,
        uBTC,
        END_UNITS
    };

    //! @name Static API
    //! Unit conversion and formatting
    ///@{

    int getColor()
    {
        return nColor;
    }
    bool setColor(int nColorIn)
    {
        if (!CheckColor(nColorIn))
        {
            return false;
        }
        nColor = nColorIn;
        unitlist = availableUnits(nColorIn);
        return true;
    }
    //! Get list of units, for drop-down box
    static QList<Unit> availableUnits(int nColorIn);
    //! Is unit ID valid?
    static bool valid(int unit, int nColorIn);
    //! Short name
    static QString name(int unit, int nColorIn);
    //! Longer description
    static QString description(int unit, int nColorIn);
    //! Number of Satoshis (1e-8) per unit
    static qint64 factor(int unit, int nColorIn);
    //! Number of amount digits (to represent max number of coins)
    static int amountDigits(int unit, int nColorIn);
    //! Number of decimals left
    static int decimals(int unit, int nColorIn);
    //! Format as string
    static QString format(int unit, qint64 amount, int nColorIn,
                                       bool plussign=false, bool localized=false);
    //! Format as string (with unit)
    static QString formatWithUnit(int unit, qint64 amount, int nColorIn,
                                       bool plussign=false, bool localized=false);
    //! Format as string (with unit) and localized
    static QString formatWithUnitLocalized(int unit, qint64 amount, int nColorIn,
                                                                bool plussign=false);
    //! Parse string to coin amount
    static bool parse(int unit, const QString &value, int nColorIn, qint64 *val_out);
    ///@}

    //! @name AbstractListModel implementation
    //! List model for unit drop-down selection box.
    ///@{
    enum RoleIndex {
        /** Unit identifier */
        UnitRole = Qt::UserRole
    };
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    ///@}
private:
    int nColor;
    QList<BitcoinUnits::Unit> unitlist;
};
typedef BitcoinUnits::Unit BitcoinUnit;

#endif // BITCOINUNITS_H
