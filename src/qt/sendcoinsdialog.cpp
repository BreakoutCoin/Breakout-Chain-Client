#include <time.h>

#include "sendcoinsdialog.h"
#include "ui_sendcoinsdialog.h"

#include "init.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "addressbookpage.h"

#include "bitcoinunits.h"
#include "addressbookpage.h"
#include "optionsmodel.h"
#include "sendcoinsentry.h"
#include "guiutil.h"
#include "askpassphrasedialog.h"

#include "coincontrol.h"
#include "coincontroldialog.h"
#include "servicetypeids.h"

#include <QMessageBox>
#include <QLocale>
#include <QTextDocument>
#include <QScrollBar>
#include <QClipboard>


SendCoinsDialog::SendCoinsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendCoinsDialog),
    model(0)
{

    // breakout service ID
    nServiceTypeID = SERVICE_NONE;

    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->addButton->setIcon(QIcon());
    ui->clearButton->setIcon(QIcon());
    ui->sendButton->setIcon(QIcon());
#endif

#if QT_VERSION >= 0x040700
    /* Do not move this to the XML file, Qt before 4.7 will choke on it */
    ui->lineEditCoinControlChange->setPlaceholderText(
                      tr("Enter a Breakout Coin address (e.g. brj6Q4FUHXjAzCRb6SB3pXPfuva5xrfjgHY)"));
    ui->editTxComment->setPlaceholderText(
                      tr("Enter a transaction comment (Note: This information is public)"));
#endif

    // selects the transaction currency
    ui->comboCurrency->setEnabled(true);

    ui->comboCurrency->addItem(COLOR_NAME[BREAKOUT_COLOR_BROSTAKE],
                               (int) BREAKOUT_COLOR_BROSTAKE);
    ui->comboCurrency->addItem(COLOR_NAME[BREAKOUT_COLOR_BROCOIN],
                               (int) BREAKOUT_COLOR_BROCOIN);
    ui->comboCurrency->addItem(COLOR_NAME[BREAKOUT_COLOR_SISCOIN],
                               (int) BREAKOUT_COLOR_SISCOIN);

    ui->comboCurrency->setCurrentIndex(1);

    connect(ui->comboCurrency, SIGNAL(activated(int)), this, SLOT(comboCurrencyActivated(int)));

    connect(ui->pushButtonServices, SIGNAL(pressed()), this, SLOT(openExist()));

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    ui->lineEditCoinControlChange->setFont(GUIUtil::bitcoinAddressFont());
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(coinControlChangeEdited(const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardPriorityAction = new QAction(tr("Copy priority"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy low output"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardPriorityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardPriority()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlPriority->addAction(clipboardPriorityAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    fNewRecipientAllowed = true;
}

///////////////////////////////////////////////////////////////////////////////
///
/// swift services: Exist
///
///////////////////////////////////////////////////////////////////////////////
int sha256_file(const char *path, char outputBuffer[65])
{
    FILE *file = fopen(path, "rb");
    if(!file) return -534;

    boost::uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    const int bufSize = 32768;
    boost::uint8_t *buffer = (boost::uint8_t *)malloc(bufSize);
    int bytesRead = 0;
    if(!buffer) return ENOMEM;
    while((bytesRead = fread(buffer, 1, bufSize, file)))
    {
        SHA256_Update(&sha256, buffer, bytesRead);
    }
    SHA256_Final(hash, &sha256);

    int i;
    for(i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    
    outputBuffer[64] = 0;

    fclose(file);
    free(buffer);
    return 0;
}


void SendCoinsDialog::openExist()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Choose a File"), tr("*.*"));
    const char *path = filename.toUtf8().constData();

    static char hashBuffer[65];
    // QString qUserFeedback;
    int result;
    result = sha256_file(path, hashBuffer);
    QString qUserFeedback;
    if (result == 0) {
        static char outputBuffer[76];

        sprintf(outputBuffer, "{\"hash\":\"%s\"}", hashBuffer);
        qUserFeedback = QString(outputBuffer);
        nServiceTypeID = SERVICE_EXIST;
    } else {
        qUserFeedback = QString("");
    }

    ui->editTxComment->setText(qUserFeedback);
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


void SendCoinsDialog::setModel(WalletModel *model)
{
    this->model = model;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->setModel(model);
        }
    }
    if(model && model->getOptionsModel())
    {
        std::map<int, qint64> mapBalance, mapStake, mapUnconfirmedBalance, mapImmatureBalance;
        std::vector<int> vCards;
        model->getBalance(GUI_OVERVIEW_COLORS, mapBalance);
        model->getStake(GUI_OVERVIEW_COLORS, mapStake);
        model->getUnconfirmedBalance(GUI_OVERVIEW_COLORS, mapUnconfirmedBalance);
        model->getImmatureBalance(GUI_OVERVIEW_COLORS, mapImmatureBalance);
        model->getHand(vCards);
        setBalance(mapBalance, mapStake, mapUnconfirmedBalance, mapImmatureBalance, vCards);
        connect(model, SIGNAL(balanceChanged(std::map<int, qint64>, std::map<int, qint64>,
                                             std::map<int, qint64>, std::map<int, qint64>,
                                             std::vector<int>)), this,
                       SLOT(setBalance(std::map<int, qint64>, std::map<int, qint64>,
                                       std::map<int, qint64>, std::map<int, qint64>,
                                       std::vector<int>)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrostake(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrocoin(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(defaultColorChanged(int)), this, SLOT(currencyChanged(int)));

        // Coin Control
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrostake(int)), this, SLOT(coinControlUpdateLabels()));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChangedBrocoin(int)), this, SLOT(coinControlUpdateLabels()));
        connect(model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        connect(model->getOptionsModel(), SIGNAL(transactionFeeChangedBrostake(qint64)), this, SLOT(coinControlUpdateLabels()));
        connect(model->getOptionsModel(), SIGNAL(transactionFeeChangedBrocoin(qint64)), this, SLOT(coinControlUpdateLabels()));
        ui->frameCoinControl->setVisible(model->getOptionsModel()->getCoinControlFeatures());

        currencyChanged(nDefaultCurrency);
    }
}


SendCoinsDialog::~SendCoinsDialog()
{
    delete ui;
}

void SendCoinsDialog::on_sendButton_clicked()
{
    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    if(!model)
        return;

    QString txcomment = ui->editTxComment->text();

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate())
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    // Format confirmation message
    QStringList formatted;
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
#if QT_VERSION < 0x050000
        formatted.append(tr("<b>%1</b> to %2 (%3)").arg(BitcoinUnits::formatWithUnitLocalized(
                   BitcoinUnits::BTC, rcp.amount, getColor()), Qt::escape(rcp.label), rcp.address));
#else
        formatted.append(tr("<b>%1</b> to %2 (%3)").arg(BitcoinUnits::formatWithUnitLocalized(
                   BitcoinUnits::BTC, rcp.amount, getColor()), rcp.label.toHtmlEscaped(), rcp.address));
#endif
    }

    fNewRecipientAllowed = false;

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
                          tr("Are you sure you want to send %1?").arg(formatted.join(tr(" and "))),
          QMessageBox::Yes|QMessageBox::Cancel,
          QMessageBox::Cancel);

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return;
    }

    WalletModel::SendCoinsReturn sendstatus;

    if (!model->getOptionsModel() || !model->getOptionsModel()->getCoinControlFeatures())
        sendstatus = model->sendCoins(txcomment, recipients, nServiceTypeID);
    else
        sendstatus = model->sendCoins(txcomment, recipients, nServiceTypeID, CoinControlDialog::coinControl);

    switch(sendstatus.status)
    {
    case WalletModel::InvalidAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The recipient address is not valid, please recheck."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::InvalidAmount:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount to pay must be larger than 0."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount exceeds your balance."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The total exceeds your balance when the %1 transaction fee is included.").
            arg(BitcoinUnits::formatWithUnitLocalized(BitcoinUnits::BTC, sendstatus.fee,
                                                                     FEE_COLOR[getColor()])),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::DuplicateAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Duplicate address found, can only send to each address once per send operation."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCreationFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: Transaction creation failed.  Please open Console and type 'clearwallettransactions' followed by 'scanforalltxns' to repair."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCommitFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: The transaction was rejected. This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::NarrationTooLong:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: Narration is too long."),
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::Aborted: // User aborted, nothing to do
        break;
    case WalletModel::OK:
        accept();
        CoinControlDialog::coinControl->UnSelectAll();
        coinControlUpdateLabels();
        break;
    case WalletModel::InvalidColor:
    case WalletModel::InconsistentAddress:
    case WalletModel::FeeExceedsFeeBalance:
        break;
    default:
        break;
    }
    fNewRecipientAllowed = true;
}

void SendCoinsDialog::clear()
{
    nServiceTypeID = SERVICE_NONE;
    ui->editTxComment->clear();

    // Remove entries until only one left
    while(ui->entries->count())
    {
        delete ui->entries->takeAt(0)->widget();
    }
    addEntry();

    updateRemoveEnabled();

    ui->sendButton->setDefault(true);
}

void SendCoinsDialog::reject()
{
    clear();
}

void SendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry *SendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    updateRemoveEnabled();

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    QCoreApplication::instance()->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());
    return entry;
}

void SendCoinsDialog::updateRemoveEnabled()
{
    // Remove buttons are enabled as soon as there is more than one send-entry
    bool enabled = (ui->entries->count() > 1);
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->setRemoveEnabled(enabled);
        }
    }
    setupTabChain(0);
    coinControlUpdateLabels();
}

void SendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    delete entry;
    updateRemoveEnabled();
}

QWidget *SendCoinsDialog::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->editTxComment);
    prev = ui->editTxComment;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->addButton);
    QWidget::setTabOrder(ui->addButton, ui->sendButton);
    return ui->sendButton;
}

void SendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
}

bool SendCoinsDialog::handleURI(const QString &uri)
{
    SendCoinsRecipient rv;
    // URI has to be valid
    if (GUIUtil::parseBitcoinURI(uri, &rv))
    {
        CBitcoinAddress address(rv.address.toStdString());
        if (!address.IsValid())
            return false;
        pasteEntry(rv);
        return true;
    }

    return false;
}

int SendCoinsDialog::getColor()
{
    // TODO: should come from model
    return ui->comboCurrency->itemData(
                 ui->comboCurrency->currentIndex()).toInt();
}

void SendCoinsDialog::setBalance(std::map<int, qint64> mapBalance,
                                 std::map<int, qint64> mapStake,
                                 std::map<int, qint64> mapUnconfirmedBalance,
                                 std::map<int, qint64> mapImmatureBalance,
                                 std::vector<int> vCards)
{
    Q_UNUSED(mapStake);
    Q_UNUSED(mapUnconfirmedBalance);
    Q_UNUSED(mapImmatureBalance);
    Q_UNUSED(vCards);

    if(!model || !model->getOptionsModel())
        return;

    int nColor = getColor();

    int unit = model->getOptionsModel()->getDisplayUnit(nColor);

    ui->labelBalance->setText(BitcoinUnits::formatWithUnitLocalized(unit,
                                                        mapBalance[nColor], nColor));
}

void SendCoinsDialog::setChangeAddressPlaceholderText(int nColor)
{
    switch(nColor)
    {
    case BREAKOUT_COLOR_BROSTAKE:
#if QT_VERSION >= 0x040700
        /* Do not move this to the XML file, Qt before 4.7 will choke on it */
        ui->lineEditCoinControlChange->setPlaceholderText(
                    tr("Enter a Breakout Stake address (e.g. bxMpqqmeKUFfFeVYJn3iHK9UvjdhApUrcNq)"));
#endif
        break;
    case BREAKOUT_COLOR_BROCOIN:
#if QT_VERSION >= 0x040700
        /* Do not move this to the XML file, Qt before 4.7 will choke on it */
        ui->lineEditCoinControlChange->setPlaceholderText(
                    tr("Enter a Breakout Coin address (e.g. brj6Q4FUHXjAzCRb6SB3pXPfuva5xrfjgHY)"));
#endif
        break;
    case BREAKOUT_COLOR_SISCOIN:
#if QT_VERSION >= 0x040700
        /* Do not move this to the XML file, Qt before 4.7 will choke on it */
        ui->lineEditCoinControlChange->setPlaceholderText(
                    tr("Enter a Sistercoin address (e.g. badaGfuYm8WdwV61dTPj58NnSkPnEPJCSKi)"));
#endif
        break;
    default:
      // If this error is thrown, edit choices for comboCurrency.
      QMessageBox::warning(this, tr("Set Currency"),
          tr("The default currency setting appears not to be valid.\n"
             "Please set the default currency preference."),
          QMessageBox::Ok, QMessageBox::Ok);
    }
}


void SendCoinsDialog::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
         std::map<int, qint64> mapBalance;
         model->getBalance(GUI_OVERVIEW_COLORS, mapBalance);
         int nColor = getColor();
         int unit =  model->getOptionsModel()->getDisplayUnit(nColor);
         ui->labelBalance->setText(
                   BitcoinUnits::formatWithUnitLocalized(unit,
                                              mapBalance[nColor], nColor));
    }
}

void SendCoinsDialog::updateCurrency(int nColor)
{
    setChangeAddressPlaceholderText(nColor);

    model->updateCurrentSendColor(nColor);
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->updateCurrency();
        }
    }
    CoinControlDialog::coinControl->SetNull();
    updateDisplayUnit();
    coinControlChangeEdited(ui->lineEditCoinControlChange->text());
    coinControlUpdateLabels();
}

void SendCoinsDialog::comboCurrencyActivated(int index)
{
    int nColor = ui->comboCurrency->itemData(index).toInt();
    updateCurrency(nColor);
}

void SendCoinsDialog::currencyChanged(int nColor)
{
    int index = ui->comboCurrency->findData(nColor);
    ui->comboCurrency->setCurrentIndex(index);
    updateCurrency(nColor);
}

// Coin Control: copy label "Quantity" to clipboard
void SendCoinsDialog::coinControlClipboardQuantity()
{
    QApplication::clipboard()->setText(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendCoinsDialog::coinControlClipboardAmount()
{
    QApplication::clipboard()->setText(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void SendCoinsDialog::coinControlClipboardFee()
{
    QApplication::clipboard()->setText(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")));
}

// Coin Control: copy label "After fee" to clipboard
void SendCoinsDialog::coinControlClipboardAfterFee()
{
    QApplication::clipboard()->setText(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")));
}

// Coin Control: copy label "Bytes" to clipboard
void SendCoinsDialog::coinControlClipboardBytes()
{
    QApplication::clipboard()->setText(ui->labelCoinControlBytes->text());
}

// Coin Control: copy label "Priority" to clipboard
void SendCoinsDialog::coinControlClipboardPriority()
{
    QApplication::clipboard()->setText(ui->labelCoinControlPriority->text());
}

// Coin Control: copy label "Low output" to clipboard
void SendCoinsDialog::coinControlClipboardLowOutput()
{
    QApplication::clipboard()->setText(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void SendCoinsDialog::coinControlClipboardChange()
{
    QApplication::clipboard()->setText(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void SendCoinsDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) // coin control features disabled
        CoinControlDialog::coinControl->SetNull();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked()
{
    CoinControlDialog dlg;
    dlg.setColor(getColor());
    dlg.setModel(model);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void SendCoinsDialog::coinControlChangeChecked(int state)
{
    if (model)
    {
        if (state == Qt::Checked)
            CoinControlDialog::coinControl->destChange = CBitcoinAddress(ui->lineEditCoinControlChange->text().toStdString()).Get();
        else
            CoinControlDialog::coinControl->destChange = CNoDestination();
    }

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
    ui->labelCoinControlChangeLabel->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void SendCoinsDialog::coinControlChangeEdited(const QString & text)
{
    if (model)
    {
        CoinControlDialog::coinControl->destChange = CBitcoinAddress(text.toStdString()).Get();

        // label for the change address
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
        if (text.isEmpty())
            ui->labelCoinControlChangeLabel->setText("");
        else if (!CBitcoinAddress(text.toStdString()).IsValid())
        {
            ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");
            std::string sName(COLOR_NAME[getColor()]);
            ui->labelCoinControlChangeLabel->setText(tr(
                     strprintf("WARNING: Invalid %s address", sName.c_str()).c_str()));
        }
        else
        {
            QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
            if (!associatedLabel.isEmpty())
            {
                ui->labelCoinControlChangeLabel->setText(associatedLabel);
            }
            else
            {
                CPubKey pubkey;
                CKeyID keyid;
                CBitcoinAddress(text.toStdString()).GetKeyID(keyid);   
                if (model->getPubKey(keyid, pubkey))
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));
                else
                {
                    ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");
                    ui->labelCoinControlChangeLabel->setText(tr("WARNING: unknown change address"));
                }
            }
            if (CBitcoinAddress(text.toStdString()).nColor != getColor())
            {
                 ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");
                 std::string sName(COLOR_NAME[getColor()]);
                 ui->labelCoinControlChangeLabel->setText(tr(
                     strprintf("WARNING: Invalid %s address", sName.c_str()).c_str()));
            }
        }
    }
}

// Coin Control: update labels
void SendCoinsDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel() || !model->getOptionsModel()->getCoinControlFeatures())
        return;

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
            CoinControlDialog::payAmounts.append(entry->getValue().amount);
    }

    if (CoinControlDialog::coinControl->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(model, this, getColor());

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}
