#include "qvaluecombobox.h"
#include "bitcoinunits.h"

QValueComboBox::QValueComboBox(QWidget *parent) :
        QComboBox(parent), role(Qt::UserRole)
{
    connect(this, SIGNAL(currentIndexChanged(int)), this, SLOT(handleSelectionChanged(int)));
}

QVariant QValueComboBox::value() const
{
    return itemData(currentIndex(), role);
}

void QValueComboBox::setValue(const QVariant &value)
{
    setCurrentIndex(findData(value, role));
}

void QValueComboBox::setRole(int role)
{
    this->role = role;
}

int QValueComboBox::getColor()
{
    BitcoinUnits *pmodel = (BitcoinUnits *) this->model();
    if (pmodel != NULL)
    {
        return pmodel->getColor();
    }
    return BREAKOUT_COLOR_NONE;
}

bool QValueComboBox::setColor(int nColor)
{
    BitcoinUnits *pmodel = (BitcoinUnits *) this->model();
    if (pmodel != NULL)
    {
        return pmodel->setColor(nColor);
    }
    return false;
}

void QValueComboBox::handleSelectionChanged(int idx)
{
    emit valueChanged();
}
