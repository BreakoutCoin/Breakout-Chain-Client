/*
 * Qt5 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 */


#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "getprivkeysdialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "bitcoinrpc.h"
#include "json_spirit.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>

#include <QInputDialog>

#include <QMimeData>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
//#include <QFileDialog> QT5 Conversion
//#include <QDesktopServices> QT5 Conversion
#include <QTimer>
#include <QDragEnterEvent>
#if QT_VERSION < 0x050000
#include <QUrl>
#endif
#include <QStyle>
#include <QProgressDialog>
#include <QActionGroup>
#include <QStandardPaths>

#include <iostream>
#include <fstream>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/err.h>


extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
extern unsigned int nTargetSpacing;

extern void StartShutdown();

double GetPoSKernelPS();

// unpack dates in this format: ["1/15/2015", "7/19/2014"]
bool UnpackDates(const QString &message, QString &current, QString &next)
{
    json_spirit::Value value;
    std::string sMessage = message.toUtf8().constData();
    if (json_spirit::read(sMessage, value))
    {
        if (value.type() != json_spirit::array_type)
        {
           if (fDebug)
           {
                printf("UnpackDates(): wrong type for dates\n");
           }
           return false;
        }
        json_spirit::Array ary = value.get_array();
        if ((ary.size() != 2) || (ary[0].type() != json_spirit::str_type) ||
                                          (ary[1].type() != json_spirit::str_type))
        {
           if (fDebug)
           {
                printf("UnpackDates(): malformed dates\n");
           }
           return false;
        }
        current = QString(ary[0].get_str().c_str());
        next = QString(ary[1].get_str().c_str());
    }
    else
    {
        if (fDebug)
        {
              printf("UnpackDates(): could not parse dates as json\n");
        }
        return false;
    }
    return true;
}
   

BitcoinGUI::BitcoinGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    rpcConsole(0)
{
    setFixedSize(1100, 640);
    setWindowTitle(tr("Breakout") + " " + tr("Wallet"));
    qApp->setStyleSheet("QMainWindow { border:none;font-family:'Open Sans,sans-serif'; } #frame { } QToolBar QLabel { padding-top:15px;padding-bottom:10px;margin:0px; } #spacer { background:rgb(8,51,119);border:none; } #progressBarLabel { color:rgb(255,255,255); padding-top:2px; padding-bottom:2px; padding-right:8px; margin:0px;} #progressBarLabel, #toolbarProgress {background-color: rgb(56,56,56);border:none;} #progressBar {background-color: rgb(128,128,128)} #toolbar3 { border:none;width:1px; background-color: rgb(56,56,56); } #toolbar2 { border:none;width:28px; background-color:rgb(56,56,56); } #toolbar { border:none;height:100%;padding-top:20px; background: rgb(8,51,119); text-align: left; color: white;min-width:150px;max-width:150px;} QToolBar QToolButton:hover {background-color:qlineargradient(x1: 0, y1: 0, x2: 1, y2: 1,stop: 0 rgb(8,51,119), stop: 1 rgb(27,88,186));} QToolBar QToolButton { font-family:Century Gothic;padding-left:20px;padding-right:150px;padding-top:10px;padding-bottom:10px; width:100%; color: white; text-align: left; background-color: rgb(8,51,119) } #labelMiningIcon { padding-left:5px;font-family:Century Gothic;width:100%;font-size:10px;text-align:center;color:white; } QMenu { background: rgb(8,51,119); color:white; padding-bottom:10px; } QMenu::item { color:white; background-color: transparent; } QMenu::item:selected { background-color:qlineargradient(x1: 0, y1: 0, x2: 0.5, y2: 0.5,stop: 0 rgb(8,51,119), stop: 1 rgb(27,88,186)); } QMenuBar { background: rgb(8,51,119); color:white; } QMenuBar::item { font-size:12px;padding-bottom:8px;padding-top:8px;padding-left:15px;padding-right:15px;color:white; background-color: transparent; } QMenuBar::item:selected { background-color:qlineargradient(x1: 0, y1: 0, x2: 0.5, y2: 0.5,stop: 0 rgb(8,51,119), stop: 1 rgb(27,88,186));}");
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/bitcoin"));
    setWindowIcon(QIcon(":icons/bitcoin"));
#else
    setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    // Create tabs
    overviewPage = new OverviewPage();

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing,
                                               AddressBookPage::SendingTab, BREAKOUT_COLOR_NONE);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing,
                                               AddressBookPage::ReceivingTab, BREAKOUT_COLOR_NONE);

    sendCoinsPage = new SendCoinsDialog(this);

    signVerifyMessageDialog = new SignVerifyMessageDialog(this);

    // make these dialogs manually because the convenience functions crash the client
    // simplePasswordDialog = 0;
    // simpleLabelDialog = 0;

    simplePasswordDialog = new QInputDialog(this, Qt::Dialog);
    simplePasswordDialog->setInputMode(QInputDialog::TextInput);
    simplePasswordDialog->setTextEchoMode(QLineEdit::Password);
    simplePasswordDialog->setLabelText(tr("Password:"));
    simplePasswordDialog->setWindowTitle(tr("Key Password"));
    simplePasswordDialog->setModal(false);
    simplePasswordDialog->setVisible(false);

    simpleLabelDialog = new QInputDialog(this, Qt::Dialog);
    simpleLabelDialog->setInputMode(QInputDialog::TextInput);
    simpleLabelDialog->setTextEchoMode(QLineEdit::Normal);
    simpleLabelDialog->setLabelText(tr("Address Book Label"));
    simpleLabelDialog->setWindowTitle(tr("Label:"));
    simpleLabelDialog->setModal(false);
    simpleLabelDialog->setVisible(false);


    centralWidget = new QStackedWidget(this);
    centralWidget->addWidget(overviewPage);
    centralWidget->addWidget(transactionsPage);
    centralWidget->addWidget(addressBookPage);
    centralWidget->addWidget(receiveCoinsPage);
    centralWidget->addWidget(sendCoinsPage);
    setCentralWidget(centralWidget);



    // Status bar notification icons

    labelEncryptionIcon = new QLabel();
    labelStakingIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();
    //actionConvertIcon = new QAction(QIcon(":/icons/statistics"), tr(""), this);

    if (GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(30 * 1000);
        updateStakingIcon();
    }

// Progress bar and label for blocks download
    progressBarLabel = new QLabel(this);
    progressBarLabel->setVisible(true);
    progressBarLabel->setObjectName("progressBarLabel");
    progressBar = new QProgressBar(this);
    progressBar->setObjectName("progressBar");
    progressBar->setVisible(true);
    addToolBarBreak(Qt::LeftToolBarArea);
    QToolBar *toolbar2 = addToolBar(tr("Tabs toolbar"));
    addToolBar(Qt::LeftToolBarArea,toolbar2);
    toolbar2->setOrientation(Qt::Vertical);
    toolbar2->setMovable( false );
    toolbar2->setObjectName("toolbar2");
    toolbar2->setFixedWidth(28);
    toolbar2->setIconSize(QSize(28,28));
    //toolbar2->addAction(actionConvertIcon);
    toolbar2->addWidget(labelEncryptionIcon);
    toolbar2->addWidget(labelStakingIcon);
    toolbar2->addWidget(labelConnectionsIcon);
    toolbar2->addWidget(labelBlocksIcon);
    toolbar2->setStyleSheet("#toolbar2 QToolButton { border:none;padding:0px;margin:0px;height:20px;width:28px;margin-top:36px; }");

    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar *toolbar3 = addToolBar(tr("Green bar"));
    addToolBar(Qt::TopToolBarArea,toolbar3);
    toolbar3->setOrientation(Qt::Horizontal);
    toolbar3->setMovable( false );
    toolbar3->setObjectName("toolbar3");
    toolbar3->setFixedHeight(2);

    syncIconMovie = new QMovie(":/movies/update_spinner", "gif", this);

    toolbarProgress = addToolBar(tr("Progress bar"));
    addToolBar(Qt::BottomToolBarArea, toolbarProgress);
    toolbarProgress->setOrientation(Qt::Horizontal);
    toolbarProgress->setMovable(false);
    toolbarProgress->setObjectName("toolbarProgress");
    toolbarProgress->setFixedHeight(28);
    toolbarProgress->addWidget(progressBarLabel);
    toolbarProgress->addWidget(progressBar);
    toolbarProgress->setVisible(false);

    // Reconciling label — spans the full bottom, hidden until reconcileStarted()
    labelReconcilingStatus = new QLabel(
        tr("⏳ Wallet is reconciling balances. Please wait."),
        this);
    labelReconcilingStatus->setSizePolicy(QSizePolicy::Expanding,
                                          QSizePolicy::Preferred);

    QToolBar *toolbarReconcilingStatus = addToolBar(tr("ReconcilingStatus"));
    addToolBar(Qt::BottomToolBarArea, toolbarReconcilingStatus);
    toolbarReconcilingStatus->setOrientation(Qt::Horizontal);
    toolbarReconcilingStatus->setMovable(false);
    toolbarReconcilingStatus->setFloatable(false);
    toolbarReconcilingStatus->addWidget(labelReconcilingStatus);
    toolbarReconcilingStatus->setVisible(false);
    toolbarReconcilingStatus->setObjectName("toolbarReconcilingStatus");

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));

    gotoOverviewPage();
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setToolTip(tr("Show general overview of wallet"));
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_1));
    tabGroup->addAction(overviewAction);

//     statisticsAction = new QAction(QIcon(":/icons/statistics"), tr("&Statistics"), this);
//     statisticsAction->setToolTip(tr("View statistics"));
//     statisticsAction->setCheckable(true);
//     tabGroup->addAction(statisticsAction);


    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setToolTip(tr("Send money to a Breakout address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/bitcoin"), tr("&About Breakout"), this);
    aboutAction->setToolTip(tr("Show information about Breakout"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(QIcon(":/icons/qtlogo"), tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for Breakout"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_encrypt"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt wallet"));
    encryptWalletAction->setCheckable(true);
    importWalletAction = new QAction(QIcon(":/icons/enc_key"), tr("&Import Encrypted Wallet Key..."), this);
    importWalletAction->setToolTip(tr("Import json wallet file with encrypted wallet key"));
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setToolTip(tr("Backup wallet to another location"));
    rebuildWalletAction = new QAction(QIcon(":/icons/options"), tr("&Rebuild Wallet"), this);
    rebuildWalletAction->setToolTip(tr("Purge and re-scan wallet"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setToolTip(tr("Change the passphrase used for wallet encryption"));
    unlockWalletAction = new QAction(QIcon(":/icons/mint_closed"), tr("&Unlock Wallet"), this);
    unlockWalletAction->setToolTip(tr("Unlock Mining"));
    lockWalletAction = new QAction(QIcon(":/icons/mint_open"), tr("&Lock Wallet"), this);
    lockWalletAction->setToolTip(tr("Lock Mining"));
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);
    getPrivkeysAction = new QAction(QIcon(":/icons/key"), tr("&Get Private Keys"), this);

    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
    connect(importWalletAction, SIGNAL(triggered()), this, SLOT(importWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(rebuildWalletAction, SIGNAL(triggered()), this, SLOT(rebuildWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
    connect(getPrivkeysAction, SIGNAL(triggered()), this, SLOT(getPrivkeys()));
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(rebuildWalletAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addSeparator();
    file->addAction(importWalletAction);
    file->addAction(getPrivkeysAction);
    file->addSeparator();
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
//    settings->addAction(unlockWalletAction);
//    settings->addAction(lockWalletAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
    QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
    toolbar->setObjectName("toolbar");
    addToolBar(Qt::LeftToolBarArea,toolbar);
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable( false );
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(50,25));
    toolbar->addAction(overviewAction);
    toolbar->addAction(sendCoinsAction);
    toolbar->addAction(receiveCoinsAction);
    toolbar->addAction(historyAction);
    toolbar->addAction(addressBookAction);
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolbar->addWidget(spacer);
    spacer->setObjectName("spacer");
    toolbar->addAction(unlockWalletAction);
    toolbar->addAction(lockWalletAction);


}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/bitcoin_testnet"));
            setWindowIcon(QIcon(":icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("Breakout client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }

            aboutAction->setIcon(QIcon(":/icons/toolbar_testnet"));
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(setNumBlocks(int,int)));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        rpcConsole->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());
    }
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Report errors from wallet thread
        connect(walletModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);

        overviewPage->setModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void BitcoinGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("Breakout client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon);
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(28,54));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to Breakout network", "", count));
}

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel || clientModel->getNumConnections() == 0)
    {
        toolbarProgress->setVisible(false);
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

        return;
    }

    QString strStatusBarWarnings = clientModel->getStatusBarWarnings();
    QString tooltip;

    if (strStatusBarWarnings.isEmpty())
    {
        progressBarLabel->setStyleSheet("");
    }
    else
    {
        progressBarLabel->setStyleSheet("#progressBarLabel {color:rgb(255,63,127);}");
    }

    if(count < nTotalBlocks)
    {
        float nPercentageDone = count / (nTotalBlocks * 0.01f);

        if (strStatusBarWarnings.isEmpty())
        {
            toolbarProgress->setVisible(true);
            progressBarLabel->setVisible(true);
            progressBarLabel->setText(tr("Synchronizing with network..."));
            progressBar->setVisible(true);
            progressBar->setFormat(tr("%n%", "", nPercentageDone));
            progressBar->setMaximum(nTotalBlocks);
            progressBar->setValue(count);
            progressBar->setStyleSheet("#progressBar {background:rgb(128,128,128);}");
        }

        tooltip = tr("Downloaded %1 of %2 blocks of transaction history (%3% done).").arg(count).arg(nTotalBlocks).arg(nPercentageDone, 0, 'f', 2);
    }
    else
    {
        if (strStatusBarWarnings.isEmpty())
        {
            toolbarProgress->setVisible(false);
            progressBarLabel->setVisible(false);
            progressBar->setVisible(false);
            progressBar->setStyleSheet("#progressBar {background:rgb(56,56,56);border:none;}");
        }
        else
        {
            toolbarProgress->setVisible(true);
            progressBarLabel->setText(strStatusBarWarnings);
            progressBarLabel->setVisible(true);
            progressBar->setVisible(false);
            progressBar->setFormat(tr(""));
            progressBar->setMaximum(100);
            progressBar->setValue(0);
            progressBar->setStyleSheet("#progressBar {background:rgb(56,56,56);border:none;}");
        }

        tooltip = tr("Downloaded %1 blocks of transaction history.").arg(count);
    }

    // Override progressBarLabel text and hide progress bar, when we have warnings to display
    if (!strStatusBarWarnings.isEmpty())
    {
        toolbarProgress->setVisible(true);
        progressBarLabel->setVisible(true);
        progressBarLabel->setText(strStatusBarWarnings);
        progressBar->setVisible(false);
        progressBar->setStyleSheet("#progressBar {background:rgb(56,56,56);border:none;}");
    }

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    int secs = lastBlockDate.secsTo(QDateTime::currentDateTime());
    QString text;

    // Represent time from last generated block in human readable text
    if(secs <= 0)
    {
        // Fully up to date. Leave text empty.
    }
    else if(secs < 60)
    {
        text = tr("%n second(s) ago","",secs);
    }
    else if(secs < 60*60)
    {
        text = tr("%n minute(s) ago","",secs/60);
    }
    else if(secs < 24*60*60)
    {
        text = tr("%n hour(s) ago","",secs/(60*60));
    }
    else
    {
        text = tr("%n day(s) ago","",secs/(60*60*24));
    }

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60 && count >= nTotalBlocks)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(28,54));

        overviewPage->showOutOfSyncWarning(false);
    }
    else
    {
        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        syncIconMovie->start();

        overviewPage->showOutOfSyncWarning(true);
    }

    if(!text.isEmpty())
    {
        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1.").arg(text);
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if(modal)
    {
        QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
    } else {
        notificator->notify(Notificator::Critical, title, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, int nColor, bool *payFee)
{
    QString strMessage =
        tr("This transaction requires a fee based on the services it uses. You may send it "
           "for a fee of %1, which rewards all users of the Breakout network as a result "
           "of your usage. Do you want to pay this fee?").arg(
                BitcoinUnits::formatWithUnit(
                       BitcoinUnits::BTC, nFeeRequired, nColor));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 qnDate = ttm->index(start, TransactionTableModel::Date, parent)
                    .data(Qt::EditRole).toULongLong();

    // Don't spam baloons except for the last hour
    if (qnDate <  (GetTime() - 60 * 60))
    {
        return;
    }

    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    QString ticker = ttm->index(start, TransactionTableModel::Ticker, parent)
                    .data(Qt::EditRole).toString();
    if(!clientModel->inInitialBlockDownload())
    {
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                        .data().toString();
        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                        .data().toString();
        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();
        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                            TransactionTableModel::ToAddress, parent)
                        .data(Qt::DecorationRole));
        int nColor;
        if (!GetColorFromTicker(ticker.toUtf8().constData(), nColor))
        {
                throw std::runtime_error(strprintf("Invalid ticker: %s", ticker.toUtf8().constData()));
        }

        int unit = walletModel->getOptionsModel()->getDisplayUnit(nColor);

        notificator->notify(Notificator::Information,
                            (amount)<0 ? tr("Sent transaction") :
                                         tr("Incoming transaction"),
                              tr("Date: %1\n"
                                 "Value: %2\n"
                                 "Type: %4\n"
                                 "Address: %5\n")
                              .arg(date)
                              .arg(BitcoinUnits::formatWithUnit(unit, amount, nColor, true))
                              .arg(type)
                              .arg(address), icon);
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralWidget->setCurrentWidget(overviewPage);
    centralWidget->setMaximumWidth(1100);
    centralWidget->setMaximumHeight(640);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

//void BitcoinGUI::gotoTradePage()
//{
//    tradeAction->setChecked(true);
//    centralWidget->setCurrentWidget(tradePage);
//	centralWidget->setMaximumWidth(850);
//	centralWidget->setMaximumHeight(520);
//
//    exportAction->setEnabled(false);
//    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
//}

// void BitcoinGUI::gotoBlockBrowser()
// {
//     blockAction->setChecked(true);
//     centralWidget->setCurrentWidget(blockBrowser);

//     exportAction->setEnabled(false);
//     disconnect(exportAction, SIGNAL(triggered()), 0, 0);
// }

// void BitcoinGUI::gotoBackupPage()
// {
//     backupAction->setChecked(true);
//     centralWidget->setCurrentWidget(backupPage);
//
//     exportAction->setEnabled(false);
//     disconnect(exportAction, SIGNAL(triggered()), 0, 0);
// }

// void BitcoinGUI::gotoChatPage()
// {
//     chatAction->setChecked(true);
//     centralWidget->setCurrentWidget(chatWindow);
//     centralWidget->setMaximumWidth(1100);
//     centralWidget->setMaximumHeight(640);
//
//     exportAction->setEnabled(false);
//     disconnect(exportAction, SIGNAL(triggered()), 0, 0);
// }

// void BitcoinGUI::gotoStatisticsPage()
// {
//     statisticsAction->setChecked(true);
//     centralWidget->setCurrentWidget(statisticsPage);
//
//     exportAction->setEnabled(false);
//     disconnect(exportAction, SIGNAL(triggered()), 0, 0);
// }

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    centralWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    centralWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (sendCoinsPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Breakout address or malformed URI parameters."));
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Breakout address or malformed URI parameters."));
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(28,54));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(28,54));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}

void BitcoinGUI::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void BitcoinGUI::importWallet()
{
    #if QT_VERSION < 0x050000
    QString openDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    #else
    QString openDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    #endif
    QString filename = QFileDialog::getOpenFileName(this, tr("Import Encrypted Wallet Key"),
                             openDir, tr("Encrypted Wallet Key (*.json)"));

    std::string strJson;
    if (filename.isEmpty())
    {
        return;
    }
    {
        std::ifstream wfile(filename.toUtf8().constData());
        if (wfile.is_open())
        {
             std::stringstream buffer;
             buffer << wfile.rdbuf();
             strJson = buffer.str();
        }
        else
        {
            QMessageBox::warning(this, tr("Open Failed"),
               tr("There was an error trying to open the file containing the encrypted wallet key."));
            return;
        }
    }

    json_spirit::Value value;
    json_spirit::read(strJson, value);
    if (value.type() != json_spirit::obj_type)
    {
        QMessageBox::warning(this, tr("Read Failed"),
              tr("The file does not appear to be in json format."));
        return;
    }

    json_spirit::Object obj = value.get_obj();

    json_spirit::Object::iterator it;

    for (it = obj.begin(); it != obj.end(); ++it)
    {
        if (it->name_ == "btcaddr")
        {
             break;
        }
    }
    if (it == obj.end())
    {
        QMessageBox::warning(this, tr("Read Failed"),
              tr("The file does not appear to be an encrypted wallet key file."));
        return;
    }
    std::string strBtcAddr = (std::string) it->value_.get_str();

    for (it = obj.begin(); it != obj.end(); ++it)
    {
        if (it->name_ == "encseed")
        {
             break;
        }
    }
    if (it == obj.end())
    {
        QMessageBox::warning(this, tr("Read Failed"),
              tr("The file does not appear to be an encrypted wallet key file."));
        return;
    }
    std::string strEncSeedHex = (std::string) it->value_.get_str();

    // coinsale wallet: key 16 bytes, rounds 2000, salt == password
    static const int IVLEN   = 16;  // AES block size (AES-128-CBC)
    static const int KEYLEN  = 16;  // AES-128 key length
    static const int ROUNDS  = 2000;
    static const int SHA3LEN = 32;  // SHA3-256 digest length

    QString qsPass;

    bool ok = simplePasswordDialog->exec();

    // QString qsPass = QInputDialog::getText(this, tr("Key Password"),
    //                                        tr("Password:"), QLineEdit::Password,
    //                                        "", &ok);

    if (ok)
    {
        qsPass = simplePasswordDialog->textValue();
    }
    else
    {
       return;
    }

    if (qsPass.isEmpty())
    {
       return;
    }

    std::string strPassword = qsPass.toStdString();
    const unsigned char* password = reinterpret_cast<const unsigned char*>(strPassword.c_str());
    size_t passlen = strPassword.size();

    // --- PBKDF2-HMAC-SHA256 key derivation (salt == password, same as original) ---
    unsigned char aDerived[KEYLEN];
    if (PKCS5_PBKDF2_HMAC(
            reinterpret_cast<const char*>(password), static_cast<int>(passlen),
            password, static_cast<int>(passlen),
            ROUNDS,
            EVP_sha256(),
            KEYLEN, aDerived) != 1)
    {
        QMessageBox::warning(this, tr("Decryption Failed"),
              tr("Key derivation failed (PBKDF2)."));
        return;
    }

    // --- Hex-decode encseed using OpenSSL BIGNUM hex decoder ---
    std::string strEncSeed;
    {
        // strEncSeedHex is an uppercase or lowercase hex string; decode it manually
        // using OpenSSL's EVP to stay dependency-free of Crypto++.
        const std::string& hex = strEncSeedHex;
        if (hex.size() % 2 != 0)
        {
            QMessageBox::warning(this, tr("Read Failed"),
                  tr("The encrypted seed has an invalid hex encoding."));
            return;
        }
        strEncSeed.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            unsigned int byte_val = 0;
            if (sscanf(hex.c_str() + i, "%02x", &byte_val) != 1)
            {
                QMessageBox::warning(this, tr("Read Failed"),
                      tr("The encrypted seed has an invalid hex encoding."));
                return;
            }
            strEncSeed.push_back(static_cast<char>(byte_val));
        }
    }

    if (static_cast<int>(strEncSeed.size()) <= IVLEN)
    {
        QMessageBox::warning(this, tr("Read Failed"),
              tr("The encrypted seed is too short."));
        return;
    }

    unsigned char aIV[IVLEN];
    memcpy(aIV, strEncSeed.data(), IVLEN);

    std::string strCText = strEncSeed.substr(IVLEN);
    std::string strPText;

    // --- AES-128-CBC decryption via OpenSSL EVP ---
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
        {
            QMessageBox::warning(this, tr("Decryption Failed"),
                  tr("Failed to allocate cipher context."));
            return;
        }

        // EVP_DecryptInit_ex uses PKCS#7 padding by default (same as Crypto++ StreamTransformationFilter)
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, aDerived, aIV) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            QMessageBox::warning(this, tr("Decryption Failed"),
                  tr(strprintf("The password '%s' is incorrect.", qsPass.toStdString().c_str()).c_str()));
            return;
        }

        const int clen = static_cast<int>(strCText.size());
        strPText.resize(clen + AES_BLOCK_SIZE); // EVP needs up to one extra block
        int outlen1 = 0, outlen2 = 0;

        if (EVP_DecryptUpdate(ctx,
                reinterpret_cast<unsigned char*>(&strPText[0]), &outlen1,
                reinterpret_cast<const unsigned char*>(strCText.data()), clen) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            QMessageBox::warning(this, tr("Decryption Failed"),
                  tr(strprintf("The password '%s' is incorrect.", qsPass.toStdString().c_str()).c_str()));
            return;
        }

        if (EVP_DecryptFinal_ex(ctx,
                reinterpret_cast<unsigned char*>(&strPText[outlen1]), &outlen2) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            // Padding check failed — wrong password
            QMessageBox::warning(this, tr("Decryption Failed"),
                  tr(strprintf("The password '%s' is incorrect.", qsPass.toStdString().c_str()).c_str()));
            return;
        }

        EVP_CIPHER_CTX_free(ctx);
        strPText.resize(outlen1 + outlen2);
    }

    // --- SHA3-256 of plaintext to derive the private key ---
    unsigned char aPrivKey[SHA3LEN];
    {
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (!mdctx)
        {
            QMessageBox::warning(this, tr("Hashing Failed"),
                  tr("Failed to allocate hash context."));
            return;
        }

        if (EVP_DigestInit_ex(mdctx, EVP_sha3_256(), nullptr) != 1 ||
            EVP_DigestUpdate(mdctx, strPText.data(), strPText.size()) != 1 ||
            EVP_DigestFinal_ex(mdctx, aPrivKey, nullptr) != 1)
        {
            EVP_MD_CTX_free(mdctx);
            QMessageBox::warning(this, tr("Hashing Failed"),
                  tr("SHA3-256 computation failed."));
            return;
        }
        EVP_MD_CTX_free(mdctx);
    }

    std::vector<unsigned char> vbyteSecret(aPrivKey, aPrivKey + SHA3LEN);

    CKey ckeySecret;
    ckeySecret.Set(vbyteSecret.begin(), vbyteSecret.end(), false);

    CPubKey pubkey = ckeySecret.GetPubKey();
    CKeyID vchAddress = pubkey.GetID();
    CBitcoinAddress address(vchAddress, nDefaultCurrency);

    std::string strLabel;

    QString qsDefaultLabel = tr("Encrypted Wallet Key");

    // QString qsLabel = QInputDialog::getText(this, tr("Addressbook Label"),
    //                                         tr("Label:"), QLineEdit::Normal,
    //                                         qsDefault, &ok);

    simpleLabelDialog->setTextValue(qsDefaultLabel);

    ok = simpleLabelDialog->exec();

    if (ok)
    {
        QString qsLabel = simpleLabelDialog->textValue();
        if (qsLabel.isEmpty())
        {
            strLabel = qsDefaultLabel.toUtf8().constData();
        }
        else
        {
            strLabel = qsLabel.toUtf8().constData();
        }
    }
    else
    {
        strLabel = qsDefaultLabel.toUtf8().constData();
    }

    CBitcoinSecret cSecret;
    cSecret.SetKey(ckeySecret);

    WalletModel::AddKeyStatus status =
                    walletModel->addPrivKey(ckeySecret, pubkey, vchAddress,
                                            nDefaultCurrency, strLabel);

    if (status == WalletModel::StatusKeyError)
    {
        QMessageBox::warning(this, tr("Add Key Failed"),
                                 tr("Could not add key, is the wallet unlocked?"));
    }
    else if (status == WalletModel::StatusKeyHad)
    {
        QMessageBox::information(this, tr("Key Present"),
              tr("The key was already present in the wallet."));
    }
    else
    {
        QMessageBox::information(this, tr("Key Added"),
              tr("The key was added successfully."));
    }

    return;
}


void BitcoinGUI::getPrivkeys()
{
    mapSecretByAddressByColor_t mapAddrs;
    walletModel->getPrivateKeys(mapAddrs);

    GetPrivkeysDialog dlg;
    if (dlg.setModel(mapAddrs, walletModel))
    {
        dlg.exec();
    }
}

void BitcoinGUI::backupWallet()
{
    #if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    #else
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    #endif
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void dialogProgress(void *d, unsigned int v)
{
    static_cast<QProgressDialog*>(d)->setValue(v);
}


void BitcoinGUI::rebuildWallet()
{
    {
      QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Breakout Chain",
                                    "Backup wallet first? (Recommended)",
                                    QMessageBox::Yes|QMessageBox::No);
      if (reply == QMessageBox::Yes) {
        this->backupWallet();
      }
    }

    {
      QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Breakout Chain",
                                    "Rebuilding takes a lot of time, proceed?",
                                    QMessageBox::Yes|QMessageBox::No);
      if (reply == QMessageBox::No) {
        return;
      }
    }


    MilliSleep(10);

    {
        QProgressDialog dialog("Rescan - Clearing wallet transactions. "
                               "Please wait.",
                               "Cancel", 0, 100, this);
        dialog.setWindowModality(Qt::WindowModal);
        dialog.setCancelButton(0);
        dialog.setMinimumDuration(0);
        dialog.setValue(0);
        dialog.show();

        CBlockIndex *pindex = pindexGenesisBlock;
        if (pindex == NULL) {
            // no reason to notify user (?)
            return;
        }

        // Updating after every change takes a long time.
        if (walletModel)
        {
            walletModel->unsubscribeFromTransactionSignal();
        }

        CProgressHelper progress(&dialogProgress, &dialog, 100);

        {

            LOCK2(cs_main, pwalletMain->cs_wallet);

            std::string strError;
            pwalletMain->ClearWalletTransactions(strError, &progress);

            dialog.setLabelText("Rescan - Scanning for wallet transactions. "
                                "Please wait.");
            progress.setEvery(1000);

            pwalletMain->MarkDirty();

            pwalletMain->ScanForWalletTransactions(pindex,
                                                   true,
                                                   &progress);

            dialog.setLabelText("Rescan - Reaccepting wallet transactions. "
                                "Please wait.");
            progress.setEvery(100);

            pwalletMain->ReacceptWalletTransactions(&progress);
        }

        if (walletModel)
        {
            walletModel->subscribeToTransactionSignal();
        }

        dialog.setValue(100);
    }

    {
        QMessageBox::information(
            this,
            "Breakout Chain",
            "Breakout Chain must quit to rebuild the transaction table.",
            QMessageBox::Ok);
        StartShutdown();
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void BitcoinGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog::Mode mode = sender() == unlockWalletAction ?
              AskPassphraseDialog::UnlockStaking : AskPassphraseDialog::Unlock;
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
    else if (fWalletUnlockStakingOnly)
    {
        AskPassphraseDialog::Mode mode = AskPassphraseDialog::UnlockStaking;
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void BitcoinGUI::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateStakingIcon()
{
    uint64_t nWeight = 0;
    if (pwalletMain)
        pwalletMain->GetStakeWeight(*pwalletMain, nWeight);

    if (nLastCoinStakeSearchInterval && nWeight)
    {

        uint64_t nNetworkWeight = GetPoSKernelPS();

#if 0
        QString text;
        unsigned nEstimateTime = nTargetSpacing * nNetworkWeight / nWeight;
        if (nEstimateTime < 60)
        {
            text = tr("%n second(s)", "", nEstimateTime);
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%n minute(s)", "", nEstimateTime/60);
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%n hour(s)", "", nEstimateTime/(60*60));
        }
        else
        {
            text = tr("%n day(s)", "", nEstimateTime/(60*60*24));
        }

        labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
# else
        labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2").arg(nWeight).arg(nNetworkWeight));
# endif
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(28,54));
    }
    else
    {
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(28,54));
        if (pwalletMain && pwalletMain->IsLocked())
            labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
        else if (vNodes.empty())
            labelStakingIcon->setToolTip(tr("Not staking because wallet is offline"));
        else if (IsInitialBlockDownload())
            labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
        else if (!nWeight)
            labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
        else
            labelStakingIcon->setToolTip(tr("Not staking"));
    }
}

void BitcoinGUI::reconcileStarted()
{
    labelReconcilingStatus->parentWidget()->setVisible(true);
}

void BitcoinGUI::reconcileEnded()
{
    labelReconcilingStatus->parentWidget()->setVisible(false);
}
