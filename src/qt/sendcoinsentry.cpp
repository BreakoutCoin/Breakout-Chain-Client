#include "sendcoinsentry.h"
#include "ui_sendcoinsentry.h"
#include "guiutil.h"
#include "bitcoinunits.h"
#include "addressbookpage.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "stealth.h"
#include "main.h"

#include <QApplication>
#include <QClipboard>

SendCoinsEntry::SendCoinsEntry(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::SendCoinsEntry),
    model(0)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC
    ui->payToLayout->setSpacing(4);
#endif
#if QT_VERSION >= 0x040700
    /* Do not move this to the XML file, Qt before 4.7 will choke on it */
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
    ui->payTo->setPlaceholderText(tr("Enter a Breakout address (e.g. brj6Q4FUHXjAzCRb6SB3pXPfuva5xrfjgHY)"));
    ui->narration->setPlaceholderText(tr("Enter a short note to send with payment (max 24 characters)"));
#endif
    setFocusPolicy(Qt::TabFocus);
    setFocusProxy(ui->payTo);

    GUIUtil::setupAddressWidget(ui->payTo, this);
    ui->narration->setMaxLength(24);
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(AddressBookPage::ForSending, AddressBookPage::SendingTab, getColor(), this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    if(!model)
        return;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
        ui->addAsLabel->setText(associatedLabel);
}

void SendCoinsEntry::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrostake(int)), this,
                                                           SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrocoin(int)), this,
                                                           SLOT(updateDisplayUnit()));
    }

    ui->payAmount->setColor(nDefaultCurrency);

    connect(ui->payAmount, SIGNAL(textChanged()), this, SIGNAL(payAmountChanged()));

    if (CheckColor(getColor()))
    {
       updateCurrency();
    }

    clear();
}

void SendCoinsEntry::setRemoveEnabled(bool enabled)
{
    ui->deleteButton->setEnabled(enabled);
}

void SendCoinsEntry::clear()
{
    // updateCurrency();
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->narration->clear();
    ui->payAmount->setValid(true);
    ui->payTo->setValid(true);
    ui->payTo->setFocus();
    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

// don't clear the user's work, just validate it
void SendCoinsEntry::updateCurrency()
{
    int nColor = getColor();
    ui->payAmount->setColor(nColor);

    QString qstt = tr("The %1 address to send payment to (e.g. %2)");

    switch(nColor)
    {
    case BREAKOUT_COLOR_BROSTAKE:
#if QT_VERSION >= 0x040700
        /* Do not move this to the XML file, Qt before 4.7 will choke on it */
        ui->payTo->setPlaceholderText(
                    tr("Enter a Brostake address (e.g. bxMpqqmeKUFfFeVYJn3iHK9UvjdhApUrcNq)"));
#endif
        ui->payTo->setToolTip(qstt.arg(COLOR_NAME[nColor]).arg("bxMpqqmeKUFfFeVYJn3iHK9UvjdhApUrcNq"));
        break;
    case BREAKOUT_COLOR_BROCOIN:
#if QT_VERSION >= 0x040700
        /* Do not move this to the XML file, Qt before 4.7 will choke on it */
        ui->payTo->setPlaceholderText(
                    tr("Enter a Brocoin address (e.g. brj6Q4FUHXjAzCRb6SB3pXPfuva5xrfjgHY)"));
#endif
        ui->payTo->setToolTip(qstt.arg(COLOR_NAME[nColor]).arg("brj6Q4FUHXjAzCRb6SB3pXPfuva5xrfjgHY"));
        break;
    case BREAKOUT_COLOR_SISCOIN:
#if QT_VERSION >= 0x040700
        /* Do not move this to the XML file, Qt before 4.7 will choke on it */
        ui->payTo->setPlaceholderText(
                    tr("Enter a Sistercoin address (e.g. badaGfuYm8WdwV61dTPj58NnSkPnEPJCSKi)"));
#endif
        ui->payTo->setToolTip(qstt.arg(COLOR_NAME[nColor]).arg("badaGfuYm8WdwV61dTPj58NnSkPnEPJCSKi"));
        break;
    default:
      // If this error is thrown, edit choices for comboCurrency.
      QMessageBox::warning(this, tr("Set Currency"),
          tr("The default currency setting appears not to be valid.\n"
             "Please set the default currency preference."),
          QMessageBox::Ok, QMessageBox::Ok);
    }

    updateDisplayUnit();
    validate(false);
    qApp->processEvents();
    this->repaint();
}

void SendCoinsEntry::on_deleteButton_clicked()
{
    emit removeEntry(this);
}

bool SendCoinsEntry::validate(bool fSend)
{
    // Check input validity
    bool retval = true;

    if (ui->payAmount->empty())
    {
        if (fSend)
        {
            ui->payAmount->setValid(false);
            retval = false;
        }
        else
        {
            ui->payAmount->setValid(true);
        }
    }
    else if (!ui->payAmount->validate())
    {
         ui->payAmount->setValid(false);
         retval = false;
    }
    else
    {
        if(ui->payAmount->value() <= 0)
        {
            // Cannot send 0 coins or less
            ui->payAmount->setValid(false);
            retval = false;
        }
        else
        {
            ui->payAmount->setValid(true);
        }
    }

    if(ui->payTo->empty())
    {
        if (fSend)
        {
            ui->payTo->setValid(false);
            retval = false;
        }
        else
        {
            ui->payTo->setValid(true);
        }
    }
    else if ((!ui->payTo->hasAcceptableInput()) ||
            (model && (model->validateAddress(ui->payTo->text()) != getColor())))
    {
        ui->payTo->setValid(false);
        retval = false;
    }
    else
    {
        ui->payTo->setValid(true);
    }

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    SendCoinsRecipient rv;

    rv.address = ui->payTo->text();
    rv.label = ui->addAsLabel->text();
    rv.narration = ui->narration->text();

    if (rv.address.length() > 75
        && IsStealthAddress(rv.address.toStdString()))
        rv.typeInd = AddressTableModel::AT_Stealth;
    else
        rv.typeInd = AddressTableModel::AT_Normal;

    rv.amount = ui->payAmount->value();

    return rv;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    QWidget::setTabOrder(ui->deleteButton, ui->addAsLabel);
    QWidget::setTabOrder(ui->addAsLabel, ui->narration);
    return ui->payAmount->setupTabChain(ui->narration);
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    ui->payTo->setText(value.address);
    ui->narration->setText(value.narration);
    ui->addAsLabel->setText(value.label);
    ui->payAmount->setValue(value.amount);
}

int SendCoinsEntry::getColor()
{
    if (model)
    {
        return model->getCurrentSendColor();
    }
    return BREAKOUT_COLOR_NONE;
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        // remove
        model->getOptionsModel();
        getColor();
        model->getOptionsModel()->getDisplayUnit(getColor());
        ui->payAmount->setDisplayUnit(
                         model->getOptionsModel()->getDisplayUnit(getColor()));
    }
}
