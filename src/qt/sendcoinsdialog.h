#ifndef SENDCOINSDIALOG_H
#define SENDCOINSDIALOG_H

#include "clientmodel.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"

#include <QWidget>

#include <QDir>
#include <QFile>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QSettings>
#include <QSlider>
#include <QFileDialog>
#include <QDesktopServices>

#include <QDialog>
#include <QString>

namespace Ui {
    class SendCoinsDialog;
}
class WalletModel;
class SendCoinsEntry;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending bitcoins */
class SendCoinsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendCoinsDialog(QWidget *parent = 0);
    ~SendCoinsDialog();

    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void pasteEntry(const SendCoinsRecipient &rv);
    bool handleURI(const QString &uri);

    int getColor();

public slots:
    void clear();
    void reject();
    void accept();
    SendCoinsEntry *addEntry();
    void updateRemoveEnabled();
    void setBalance(std::map<int, qint64> mapBalance,
                    std::map<int, qint64> mapStake,
                    std::map<int, qint64> mapUnconfirmedBalance,
                    std::map<int, qint64> mapImmatureBalance,
                    std::vector<int> vCards);

    // exist
    void openExist();

private:
    Ui::SendCoinsDialog *ui;
    WalletModel *model;
    bool fNewRecipientAllowed;
    unsigned int nServiceTypeID;

    void setChangeAddressPlaceholderText(int nColor);

    void updateCurrency(int);

private slots:
    void on_sendButton_clicked();
    void removeEntry(SendCoinsEntry* entry);
    void currencyChanged(int);
    void comboCurrencyActivated(int);
    void updateDisplayUnit();
    void coinControlFeatureChanged(bool checked);
    void coinControlButtonClicked();
    void coinControlChangeChecked(int);
    void coinControlChangeEdited(const QString &);
    void coinControlUpdateLabels();
    void coinControlClipboardQuantity();
    void coinControlClipboardAmount();
    void coinControlClipboardFee();
    void coinControlClipboardAfterFee();
    void coinControlClipboardBytes();
    void coinControlClipboardPriority();
    void coinControlClipboardLowOutput();
    void coinControlClipboardChange();
};

#endif // SENDCOINSDIALOG_H
