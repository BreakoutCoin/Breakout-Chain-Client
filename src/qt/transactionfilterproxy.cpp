#include "transactionfilterproxy.h"

#include "transactiontablemodel.h"
#include "transactionrecord.h"

#include <QDateTime>

#include <cstdlib>

#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
// Earliest date that can be represented (far in the past)
const QDateTime TransactionFilterProxy::MIN_DATE = QDateTime::fromSecsSinceEpoch(0);
// Last date that can be represented (far in the future)
const QDateTime TransactionFilterProxy::MAX_DATE = QDateTime::fromSecsSinceEpoch(0xFFFFFFFF);
#else
const QDateTime TransactionFilterProxy::MIN_DATE = QDateTime::fromTime_t(0);
const QDateTime TransactionFilterProxy::MAX_DATE = QDateTime::fromTime_t(0xFFFFFFFF);
#endif

TransactionFilterProxy::TransactionFilterProxy(QObject *parent) :
    QSortFilterProxyModel(parent),
    dateFrom(MIN_DATE),
    dateTo(MAX_DATE),
    addrPrefix(),
    typeFilter(ALL_TYPES),
    minAmount(0),
    limitRows(-1),
    showInactive(true)
{
}

bool TransactionFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    int type = index.data(TransactionTableModel::TypeRole).toInt();
    QDateTime datetime = index.data(TransactionTableModel::DateRole).toDateTime();
    QString address = index.data(TransactionTableModel::AddressRole).toString();
    QString label = index.data(TransactionTableModel::LabelRole).toString();
    qint64 amount = llabs(index.data(TransactionTableModel::AmountRole).toLongLong());
    int status = index.data(TransactionTableModel::StatusRole).toInt();

    if(!showInactive && (status == TransactionStatus::Conflicted || status == TransactionStatus::NotAccepted))
        return false;
    if(!(TYPE(type) & typeFilter))
        return false;
    if(datetime < dateFrom || datetime > dateTo)
        return false;
    if (!address.contains(addrPrefix, Qt::CaseInsensitive) && !label.contains(addrPrefix, Qt::CaseInsensitive))
        return false;
    if(amount < minAmount)
        return false;

    return true;
}

void TransactionFilterProxy::refilter()
{
    // QSortFilterProxyModel::beginFilterChange() arrived in Qt 6.9 and
    // endFilterChange() in Qt 6.10; invalidateFilter() is what came before and
    // is deprecated from Qt 6.13.  Use whichever the Qt being built against
    // actually has, so this builds on the Qt versions distributions still ship
    // as well as on current ones.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange();
#else
    invalidateFilter();
#endif
}

void TransactionFilterProxy::setDateRange(const QDateTime &from, const QDateTime &to)
{
    this->dateFrom = from;
    this->dateTo = to;
    refilter();
}

void TransactionFilterProxy::setAddressPrefix(const QString &addrPrefix)
{
    this->addrPrefix = addrPrefix;
    refilter();
}

void TransactionFilterProxy::setTypeFilter(quint32 modes)
{
    this->typeFilter = modes;
    refilter();
}

void TransactionFilterProxy::setMinAmount(qint64 minimum)
{
    this->minAmount = minimum;
    refilter();
}

void TransactionFilterProxy::setLimit(int limit)
{
    this->limitRows = limit;
}

void TransactionFilterProxy::setShowInactive(bool showInactive)
{
    this->showInactive = showInactive;
    refilter();
}

int TransactionFilterProxy::rowCount(const QModelIndex &parent) const
{
    if(limitRows != -1)
    {
        return std::min(QSortFilterProxyModel::rowCount(parent), limitRows);
    }
    else
    {
        return QSortFilterProxyModel::rowCount(parent);
    }
}
