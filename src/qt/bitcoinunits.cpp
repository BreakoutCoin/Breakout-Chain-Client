#include <stdexcept>

#include "bitcoinunits.h"
#include "util.h"

#include <QStringList>
#include <QLocale>

BitcoinUnits::BitcoinUnits(QObject *parent, int nColorIn):
        QAbstractListModel(parent),
        nColor(nColorIn),
        unitlist(availableUnits(nColorIn))
{
}

QList<BitcoinUnits::Unit> BitcoinUnits::availableUnits(int nColorIn)
{
    if (!CheckColor(nColorIn))
    {
        throw std::runtime_error(
               strprintf("availableUnts() : currency %d not valid", nColorIn).c_str());
    }
    if (COLOR_UNITS[nColorIn] < 0 || (COLOR_UNITS[nColorIn] - 1) >= BitcoinUnits::END_UNITS)
    {
        throw std::runtime_error(
               strprintf("availableUnts() : number of units for currency %d not valid", nColorIn).c_str());
    }
    QList<BitcoinUnits::Unit> unitlist;
    for (int i = 0; i < COLOR_UNITS[nColorIn]; ++i)
    {
        unitlist.append((BitcoinUnits::Unit) i);
    }
    return unitlist;
}

bool BitcoinUnits::valid(int unit, int nColorIn)
{
    if (unit == BitcoinUnits::BTC)
    {
       return true;
    }
    if (!CheckColor(nColorIn))
    {
       return false;
    }
    if (unit >= COLOR_UNITS[nColorIn])
    {
       return false;
    }
    return true;
}

QString BitcoinUnits::name(int unit, int nColorIn)
{
    if (!valid(unit, nColorIn))
    {
        return QString("???");
    }
    QString qsTicker(COLOR_TICKER[nColorIn]);
    switch(unit)
    {
    case BTC: return qsTicker;
    case mBTC: return QString("m") + qsTicker;
    case uBTC: return QString::fromUtf8("μ") + qsTicker;
    default: return QString("???");
    }
}

QString BitcoinUnits::description(int unit, int nColorIn)
{
    if (!valid(unit, nColorIn))
    {
        return QString("???");
    }
    QString qsName(COLOR_NAME[nColorIn]);
    switch(unit)
    {
    case BTC: return qsName;
    case mBTC: return QString("Milli-%1 (1 / 1,000)").arg(qsName);
    case uBTC: return QString("Micro-%1 (1 / 1,000,000)").arg(qsName);
    default: return QString("???");
    }
}

qint64 BitcoinUnits::factor(int unit, int nColorIn)
{
    if (!CheckColor(nColorIn))
    {
        return BASE_COIN;
    }
    int64_t u = COIN[nColorIn];
    switch(unit)
    {
    case BTC:
        u = COIN[nColorIn];
        break;
    case mBTC:
        u = COIN[nColorIn] / 1000;
        break;
    case uBTC:
        u = COIN[nColorIn] / 1000000;
        break;
    default:
        u = COIN[nColorIn];
    }
    if (u <= 0)
    {
        u = COIN[nColorIn];
    }
    return u;
}

int BitcoinUnits::amountDigits(int unit, int nColorIn)
{
    if (!CheckColor(nColorIn))
    {
        return 0;
    }
    int d = DIGITS[nColorIn];
    switch(unit)
    {
    case BTC: return d; // 21,000,000 (# digits, without commas)
    case mBTC: return d + 3; // 21,000,000,000
    case uBTC: return d + 6; // 21,000,000,000,000
    default: return 0;
    }
}

int BitcoinUnits::decimals(int unit, int nColorIn)
{
    if (!CheckColor(nColorIn))
    {
        return 0;
    }
    int d = DECIMALS[nColorIn];
    switch(unit)
    {
    case BTC: return std::max(d, 0);
    case mBTC: return std::max(d - 3, 0);
    case uBTC: return std::max(d - 6, 0);
    default: return 0;
    }
}

QString BitcoinUnits::format(int unit, qint64 n, int nColorIn,
                                           bool fPlus, bool localized)
{
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    if(!valid(unit, nColorIn))
    {
        return QString(); // Refuse to format invalid unit
    }

    QLocale locale = QLocale::c();
    QString decimal(".");
    if (localized)
    {
         decimal = QString(locale.decimalPoint());
    }

    qint64 coin = factor(unit, nColorIn);
    int num_decimals = decimals(unit, nColorIn);
    qint64 n_abs = (n > 0 ? n : -n);
    qint64 quotient = n_abs / coin;
    qint64 remainder = n_abs % coin;
    QString quotient_str = QString::number(quotient);
    QString remainder_str = QString::number(remainder).rightJustified(num_decimals, '0');

    // Right-trim excess zeros after the decimal point
    int nTrim = 0;
    for (int i = remainder_str.size()-1; i>=2 && (remainder_str.at(i) == '0'); --i)
        ++nTrim;
    remainder_str.chop(nTrim);

    if (localized)
    {
       QChar thousands = locale.groupSeparator();
       int N(quotient_str.size());
       for (int i = 3; i < N; ++i)
       {
           if (i % 3 == 0)
           {
               quotient_str.insert(N - i, thousands);
           }
       }
    }
    if (n < 0)
        quotient_str.insert(0, '-');
    else if (fPlus && n > 0)
        quotient_str.insert(0, '+');
    if (DECIMALS[nColorIn] == 0)
    {
        // if (remainder != 0)
        // {
        //     printf("Remainder for atomic currency is nonzero: %" PRId64, remainder);
        // }
        return quotient_str;
    }
    else
    {
        if (localized)
        {
            QChar thousandths(' ');
            int N(remainder_str.size());
            int j = 0;
            for (int i = 3; i < N; ++i)
            {
                if (i % 3 == 0)
                {
                    remainder_str.insert(i + j, thousandths);
                    ++j;
                }
            }
        }
        return quotient_str + decimal + remainder_str;
    }
}

QString BitcoinUnits::formatWithUnit(int unit, qint64 amount, int nColorIn,
                                                   bool plussign, bool localized)
{
    return format(unit, amount, nColorIn, plussign, localized) +
                                               QString(" ") + name(unit, nColorIn);
}

QString BitcoinUnits::formatWithUnitLocalized(int unit, qint64 amount,
                                              int nColorIn, bool plussign)
{
    return format(unit, amount, nColorIn, plussign, true) +
                                    QString(" ") + name(unit, nColorIn);
}

bool BitcoinUnits::parse(int unit, const QString &value, int nColorIn, qint64 *val_out)
{
    if(!valid(unit, nColorIn) || value.isEmpty())
        return false; // Refuse to parse invalid unit or empty string
    int num_decimals = decimals(unit, nColorIn);
    QStringList parts = value.split(".");

    if(parts.size() > 2)
    {
        if (fDebug)
        {
              printf("BitcoinUnits::parse: More than one dot!\n");
        }
        return false; // More than one dot
    }
    QString whole = parts[0];
    QString decimals;

    if(parts.size() > 1)
    {
        bool fNonzero = false;
        for (int i = 0; i < parts[1].size(); ++i)
        {
            if (parts[1][i] != '0')
            {
                fNonzero = true;
                break;
            }
        }
        if (fNonzero)
        {
            decimals = parts[1];
        }
    }

    if(decimals.size() > num_decimals)
    {
        if (fDebug)
        {
              printf("BitcoinUnits::parse: Size %d exceeds # decimals %d\n",
                              decimals.size(), num_decimals);
        }
        return false; // Exceeds max precision
    }
    bool ok = false;
    QString str = whole + decimals.leftJustified(num_decimals, '0');
    if (!ok)
    {
        if (fDebug)
        {
              printf("BitcoinUnits::parse: Couldn't left justify '%s'\n",
                              str.toUtf8().constData());
        }
    }

    if(str.size() > 18)
    {
        if (fDebug)
        {
              printf("BitcoinUnits::parse: String size (%d) too big\n",
                              str.size());
        }
        return false; // Longer numbers will exceed 63 bits
    }
    qint64 retvalue = str.toLongLong(&ok);
    if (!ok)
    {
        if (fDebug)
        {
              printf("BitcoinUnits::parse: Couldn't convert to LL '%s'\n",
                              str.toUtf8().constData());
        }
    }

    if(val_out)
    {
        *val_out = retvalue;
    }
    return ok;
}

int BitcoinUnits::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return unitlist.size();
}

QVariant BitcoinUnits::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if(row >= 0 && row < unitlist.size())
    {
        Unit unit = unitlist.at(row);
        switch(role)
        {
        case Qt::EditRole:
        case Qt::DisplayRole:
            return QVariant(name(unit, nColor));
        case Qt::ToolTipRole:
            return QVariant(description(unit, nColor));
        case UnitRole:
            return QVariant(static_cast<int>(unit));
        }
    }
    return QVariant();
}
