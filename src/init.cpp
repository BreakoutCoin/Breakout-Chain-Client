// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "txdb.h"
#include "walletdb.h"
#include "bitcoinrpc.h"
#include "net.h"
#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "checkpoints.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <openssl/crypto.h>

#ifndef WIN32
#include <signal.h>
#endif

unsigned short onion_port = TOR_PORT;
unsigned short p2p_port = GetDefaultPort();

using namespace std;
using namespace boost;

CWallet* pwalletMain;
CClientUIInterface uiInterface;
std::string strWalletFileName;
bool fConfChange;
bool fEnforceCanonical;
unsigned int nNodeLifespan;
unsigned int nDerivationMethodIndex;
unsigned int nMinerSleep;
bool fUseFastIndex;
enum Checkpoints::CPMode CheckpointsMode;

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

void ExitTimeout(void* parg)
{
#ifdef WIN32
    MilliSleep(5000);
    ExitProcess(0);
#endif
}

void StartShutdown()
{
#ifdef QT_GUI
    // ensure we leave the Qt main loop for a clean GUI exit (Shutdown() is called in bitcoin.cpp afterwards)
    uiInterface.QueueShutdown();
#else
    // Without UI, Shutdown() can simply be started in a new thread
    NewThread(Shutdown, NULL);
#endif
}

void Shutdown(void* parg)
{
    static CCriticalSection cs_Shutdown;
    static bool fTaken;

    // Make this thread recognisable as the shutdown thread
    RenameThread("breakout-shutoff");

    bool fFirstThread = false;
    {
        TRY_LOCK(cs_Shutdown, lockShutdown);
        if (lockShutdown)
        {
            fFirstThread = !fTaken;
            fTaken = true;
        }
    }
    static bool fExit;
    if (fFirstThread)
    {
        fShutdown = true;
        nTransactionsUpdated++;
//        CTxDB().Close();
        bitdb.Flush(false);
        StopNode();
        bitdb.Flush(true);
        boost::filesystem::remove(GetPidFile());
        UnregisterWallet(pwalletMain);
        delete pwalletMain;
        NewThread(ExitTimeout, NULL);
        MilliSleep(50);
        printf("breakout exited\n\n");
        fExit = true;
#ifndef QT_GUI
        // ensure non-UI client gets exited here, but let Bitcoin-Qt reach 'return 0;' in bitcoin.cpp
        exit(0);
#endif
    }
    else
    {
        while (!fExit)
            MilliSleep(500);
        MilliSleep(100);
        ExitThread(0);
    }
}

void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}





//////////////////////////////////////////////////////////////////////////////
//
// Start
//
#if !defined(QT_GUI)
bool AppInit(int argc, char* argv[])
{
    bool fRet = false;
    try
    {
        //
        // Parameters
        //
        // If Qt is used, parameters/bitcoin.conf are parsed in qt/bitcoin.cpp's main()
        ParseParameters(argc, argv);
        if (!boost::filesystem::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified directory does not exist\n");
            Shutdown(NULL);
        }
        ReadConfigFile(mapArgs, mapMultiArgs);

        if (mapArgs.count("-?") || mapArgs.count("--help"))
        {
            // First part of help message is specific to bitcoind / RPC client
            std::string strUsage = _("breakout version") + " " + FormatFullVersion() + "\n\n" +
                _("Usage:") + "\n" +
                  "  breakoutd [options]                     " + "\n" +
                  "  breakoutd [options] <command> [params]  " + _("Send command to -server or breakoutd") + "\n" +
                  "  breakoutd [options] help                " + _("List commands") + "\n" +
                  "  breakoutd [options] help <command>      " + _("Get help for a command") + "\n";

            strUsage += "\n" + HelpMessage();

            fprintf(stdout, "%s", strUsage.c_str());
            return false;
        }

        // Command-line RPC
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "breakout:"))
                fCommandLine = true;

        if (fCommandLine)
        {
            int ret = CommandLineRPC(argc, argv);
            exit(ret);
        }

        fRet = AppInit2();
    }
    catch (std::exception& e) {
        PrintException(&e, "AppInit()");
    } catch (...) {
        PrintException(NULL, "AppInit()");
    }
    if (!fRet)
        Shutdown(NULL);
    return fRet;
}

extern void noui_connect();
int main(int argc, char* argv[])
{
    bool fRet = false;

    // Connect bitcoind signal handlers
    noui_connect();

    fRet = AppInit(argc, argv);

    if (fRet && fDaemon)
        return 0;

    return 1;
}
#endif

bool static InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, _("breakout"), CClientUIInterface::OK | CClientUIInterface::MODAL);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, _("breakout"), CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION | CClientUIInterface::MODAL);
    return true;
}


bool static Bind(const CService &addr, bool fError = true) {
    if (IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError)) {
        if (fError)
            return InitError(strError);
        return false;
    }
    return true;
}

// Core-specific options shared between UI and daemon
std::string HelpMessage()
{
    string strUsage = _("Options:") + "\n" +
        "  -?                     " + _("This help message") + "\n" +
        "  -conf=<file>           " + _("Specify configuration file (default: breakout.conf)") + "\n" +
        "  -pid=<file>            " + _("Specify pid file (default: breakoutd.pid)") + "\n" +
        "  -datadir=<dir>         " + _("Specify data directory") + "\n" +
        "  -wallet=<dir>          " + _("Specify wallet file (within data directory)") + "\n" +
        "  -dbcache=<n>           " + _("Set database cache size in megabytes (default: 25)") + "\n" +
        "  -dblogsize=<n>         " + _("Set database disk log size in megabytes (default: 100)") + "\n" +
        "  -timeout=<n>           " + _("Specify connection timeout in milliseconds (default: 5000)") + "\n" +
        "  -proxy=<ip:port>       " + _("Connect through socks proxy") + "\n" +
        "  -socks=<n>             " + _("Select the version of socks proxy to use (4-5, default: 5)") + "\n" +
        "  -torext=<ip:port>      " + _("Use proxy to reach tor hidden services (default: same as -proxy)") + "\n"
        "  -dns                   " + _("Allow DNS lookups for -addnode, -seednode and -connect") + "\n" +
        "  -port=<port>           " + strprintf(_("Listen for connections on <port> (default: %d or testnet: %d)"),
                                                                          (int) P2P_PORT, (int) P2P_PORT_TESTNET) + "\n" +
        "  -torport=<port>        " + _("Connect to internal Tor through <torport> (default: 48155)") + "\n" +
        "  -maxconnections=<n>    " + _("Maintain at most <n> connections to peers (default: 125)") + "\n" +
        "  -addnode=<ip>          " + _("Add a node to connect to and attempt to keep the connection open") + "\n" +
        "  -connect=<ip>          " + _("Connect only to the specified node(s)") + "\n" +
        "  -seednode=<ip>         " + _("Connect to a node to retrieve peer addresses, and disconnect") + "\n" +
        "  -externalip=<ip>       " + _("Specify your own public address") + "\n" +
        "  -onlynet=<net>         " + _("Only connect to nodes in network <net> (IPv4, IPv6 or Tor)") + "\n" +

        "  -onionseed             " + _("Find peers using .onion seeds (default: 1 unless -connect)") + "\n" +
        "  -discover              " + _("Discover own IP address (default: 1 when listening and no -externalip)") + "\n" +
        "  -irc                   " + _("Find peers using internet relay chat (default: 0)") + "\n" +
        "  -listen                " + _("Accept connections from outside (default: 1 if no -proxy or -connect)") + "\n" +
        "  -bind=<addr>           " + _("Bind to given address. Use [host]:port notation for IPv6") + "\n" +
        "  -dnsseed               " + _("Find peers using DNS lookup (default: 1)") + "\n" +
        "  -staking               " + _("Stake your coins to support network and gain reward (default: 1)") + "\n" +
        "  -synctime              " + _("Sync time with other nodes. Disable if time on your system is precise e.g. syncing with NTP (default: 1)") + "\n" +
        "  -cppolicy              " + _("Sync checkpoints policy (default: strict)") + "\n" +
        "  -banscore=<n>          " + _("Threshold for disconnecting misbehaving peers (default: 100)") + "\n" +
        "  -bantime=<n>           " + _("Number of seconds to keep misbehaving peers from reconnecting (default: 86400)") + "\n" +
        "  -maxreceivebuffer=<n>  " + _("Maximum per-connection receive buffer, <n>*1000 bytes (default: 5000)") + "\n" +
        "  -maxsendbuffer=<n>     " + _("Maximum per-connection send buffer, <n>*1000 bytes (default: 1000)") + "\n" +
#ifdef USE_UPNP
#if USE_UPNP
        "  -upnp                  " + _("Use UPnP to map the listening port (default: 1 when listening)") + "\n" +
#else
        "  -upnp                  " + _("Use UPnP to map the listening port (default: 0)") + "\n" +
#endif
#endif
        "  -detachdb              " + _("Detach block and address databases. Increases shutdown time (default: 0)") + "\n" +
        "  -defaultcurrency=<ticker>   " + _("Sensible choices are BRX (BroStake) or BRO (BroCoin)") + "\n" +
        "  -defaultstake=<ticker>   " + _("Sensible choices are BRX (BroStake) or BRO (BroCoin)") + "\n" +
        "  -reservebalance=<amt>    " + _("Amount to reserve that will not stake for default stake") + "\n" +
        "  -reservebalance_<N>=<amt>    " + _("Amount to reserve that will not stake for color <N> where <N> is an int") + "\n" +
        "  -reservebalance_<ticker>=<amt>    " + _("Amount to reserve that will not stake for currency with <ticker>") + "\n" +
        "  -paytxfee=<amt>        " + _("Fee per KB to add to transactions you send for default currency") + "\n" +
        "  -paytxfee_<N>=<amt>        " + _("Fee per KB to add to transactions you send for color currency <N>") + "\n" +
        "  -paytxfee_<ticker>=<amt>        " + _("Fee per KB to add to transactions you send for currency with <ticker>") + "\n" +

        "  -mininput=<amt>        " + _("When creating transactions, ignore inputs with value less than this for default currency (default: 0.01)") + "\n" +
        "  -mininput_<N>=<amt>        " + _("When creating transactions, ignore inputs with value less than this for color currency <N> (default: 0.01)") + "\n" +
        "  -mininput_<ticker>=<amt>        " + _("When creating transactions, ignore inputs with value less than this for currency with <ticker> (default: 0.01)") + "\n" +

#ifdef QT_GUI
        "  -server                " + _("Accept command line and JSON-RPC commands") + "\n" +
#endif
#if !defined(WIN32) && !defined(QT_GUI)
        "  -daemon                " + _("Run in the background as a daemon and accept commands") + "\n" +
#endif
        "  -testnet               " + _("Use the test network") + "\n" +
        "  -debug                 " + _("Output extra debugging information. Implies all other -debug* options") + "\n" +
        "  -debugnet              " + _("Output extra network debugging information") + "\n" +
        "  -logtimestamps         " + _("Prepend debug output with timestamp") + "\n" +
        "  -shrinkdebugfile       " + _("Shrink debug.log file on client startup (default: 1 when no -debug)") + "\n" +
        "  -printtoconsole        " + _("Send trace/debug info to console instead of debug.log file") + "\n" +
#ifdef WIN32
        "  -printtodebugger       " + _("Send trace/debug info to debugger") + "\n" +
#endif
        "  -rpcuser=<user>        " + _("Username for JSON-RPC connections") + "\n" +
        "  -rpcpassword=<pw>      " + _("Password for JSON-RPC connections") + "\n" +
        "  -rpcport=<port>        " + _("Listen for JSON-RPC connections on <port> (default: 50542 or testnet: 60542)") + "\n" +
        "  -rpcallowip=<ip>       " + _("Allow JSON-RPC connections from specified IP address") + "\n" +
        "  -rpcconnect=<ip>       " + _("Send commands to node running on <ip> (default: 127.0.0.1)") + "\n" +
        "  -blocknotify=<cmd>     " + _("Execute command when the best block changes (%s in cmd is replaced by block hash)") + "\n" +
        "  -walletnotify=<cmd>    " + _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)") + "\n" +
        "  -confchange            " + _("Require a confirmations for change (default: 0)") + "\n" +
        "  -enforcecanonical      " + _("Enforce transaction scripts to use canonical PUSH operators (default: 1)") + "\n" +
        "  -alertnotify=<cmd>     " + _("Execute command when a relevant alert is received (%s in cmd is replaced by message)") + "\n" +
        "  -upgradewallet         " + _("Upgrade wallet to latest format") + "\n" +
        "  -keypool=<n>           " + _("Set key pool size to <n> (default: 100)") + "\n" +
        "  -rescan                " + _("Rescan the block chain for missing wallet transactions") + "\n" +
        "  -salvagewallet         " + _("Attempt to recover private keys from a corrupt wallet.dat") + "\n" +
        "  -checkblocks=<n>       " + _("How many blocks to check at startup (default: 2500, 0 = all)") + "\n" +
        "  -checklevel=<n>        " + _("How thorough the block verification is (0-6, default: 1)") + "\n" +
        "  -loadblock=<file>      " + _("Imports blocks from external blk000?.dat file") + "\n" +

        "\n" + _("Block creation options:") + "\n" +
        "  -blockminsize=<n>      "   + _("Set minimum block size in bytes (default: 0)") + "\n" +
        "  -blockmaxsize=<n>      "   + _("Set maximum block size in bytes (default: 1000000)") + "\n" +
        "  -blockprioritysize=<n> "   + _("Set maximum size of high-priority/low-fee transactions in bytes (default: 27000)") + "\n" +

        "\n" + _("SSL options: (see the Bitcoin Wiki for SSL setup instructions)") + "\n" +
        "  -rpcssl                                  " + _("Use OpenSSL (https) for JSON-RPC connections") + "\n" +
        "  -rpcsslcertificatechainfile=<file.cert>  " + _("Server certificate file (default: server.cert)") + "\n" +
        "  -rpcsslprivatekeyfile=<file.pem>         " + _("Server private key (default: server.pem)") + "\n" +
        "  -rpcsslciphers=<ciphers>                 " + _("Acceptable ciphers (default: TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH)") + "\n" +


        "  -burnkey=<key>    " + _("Random string") + "\n" ;

    return strUsage;
}

/** Initialize bitcoin.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2()
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
// We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
// which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif
#ifndef WIN32
    umask(077);

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);
#endif

    // basic multicurrency checks and setup
    {
        int nColorBytes = 1;
        int n = N_COLORS;
        while (n >= 256)    // byte is 0 - 255
        {
             ++nColorBytes;
             n = n >> 8;   // byte is 8 bits
        }
        assert (nColorBytes == N_COLOR_BYTES);
    }

    // check N_COLORS at runtime
    assert ((int) BREAKOUT_COLOR_END == N_COLORS);

    // create MAPS_COLOR_ID to look up values in case there are many currencies
    {
         for (int ver = 0; ver < N_VERSIONS; ++ver)
         {
              std::map<std::vector <unsigned char>, int> mapVer;
              for (int nColor = 0; nColor < N_COLORS; ++nColor)
              {
                for (int i = 0; i < N_COLOR_BYTES; ++i)
                {
                   COLOR_ID[ver][nColor][i] = aColorID[ver][nColor][i];
                }
                std::vector<unsigned char>* pkey = &COLOR_ID[ver][nColor];
                // Make sure that keys are unique within each version at least.
                // WHY is this assert here? Keys must be be unique
                //    within each version. See aColorID in colors.cpp.
                assert (mapVer.find(*pkey) == mapVer.end());
                mapVer[*pkey] = nColor;
              }
              MAPS_COLOR_ID.push_back(mapVer);
         }
    }

    // fill 3D vector COLOR_ID from aColorID
    {
        for (int i = 0; i < N_VERSIONS; ++i)
        {
            for (int j = 0; j < N_COLORS; ++j)
            {
                COLOR_ID[i][j].assign(aColorID[i][j], aColorID[i][j] + N_COLOR_BYTES);
            }
        }
    }

    // set the number of staking currencies
    {
        std::set<int> setCurr;
        for (int nColor = 1; nColor < N_COLORS; ++nColor)
        {
             int nMintColor = (int) MINT_COLOR[nColor];
             if (nMintColor != (int) BREAKOUT_COLOR_NONE)
             {
                  setCurr.insert(nMintColor);
             }
        }
        nNumberOfStakingCurrencies = setCurr.size();
        if (nNumberOfStakingCurrencies == 1)
        {
               nDefaultStake = *setCurr.begin();
        }
    }

    // overview  and deck colors may not necessarily be just for gui so fill here
    GUI_OVERVIEW_COLORS.assign(aGuiOverviewColors,
                               aGuiOverviewColors + N_GUI_OVERVIEW_COLORS);
    GUI_DECK_COLORS.assign(aGuiDeckColors,
                           aGuiDeckColors + N_GUI_DECK_COLORS);


    // ********************************************************* Step 2: parameter interactions

    nNodeLifespan = GetArg("-addrlifespan", 7);
    fUseFastIndex = GetBoolArg("-fastindex", true);
    nMinerSleep = GetArg("-minersleep", 500);

    CheckpointsMode = Checkpoints::STRICT;
    std::string strCpMode = GetArg("-cppolicy", "strict");

    if(strCpMode == "strict")
        CheckpointsMode = Checkpoints::STRICT;

    if(strCpMode == "advisory")
        CheckpointsMode = Checkpoints::ADVISORY;

    if(strCpMode == "permissive")
        CheckpointsMode = Checkpoints::PERMISSIVE;

    nDerivationMethodIndex = 0;

#if TESTNET_BUILD
    fTestNet = GetBoolArg("-testnet", true);
#else
    fTestNet = GetBoolArg("-testnet");
#endif

    if (fTestNet) {
       nTestNet = 1;
    } else {
       nTestNet = 0;
    }
    
    if (fTestNet)
    {
        SoftSetBoolArg("-irc", true);
    }

    if (mapArgs.count("-bind")) {
        // when specifying an explicit binding address, you want to listen on it
        // even when -connect or -proxy is specified
        SoftSetBoolArg("-listen", true);
    }

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via .onion, or listen by default
        SoftSetBoolArg("-onionseed", false);
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        SoftSetBoolArg("-dnsseed", false);
        SoftSetBoolArg("-listen", false);
    }

    if (mapArgs.count("-proxy")) {
        // to protect privacy, do not listen by default if a proxy server is specified
        SoftSetBoolArg("-listen", false);
    }

    if (!GetBoolArg("-listen", true)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        SoftSetBoolArg("-upnp", false);
        SoftSetBoolArg("-discover", false);
    }

    if (mapArgs.count("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        SoftSetBoolArg("-discover", false);
    }

    if (GetBoolArg("-salvagewallet")) {
        // Rewrite just private keys: rescan to find transactions
        SoftSetBoolArg("-rescan", true);
    }

    // ********************************************************* Step 3: parameter-to-internal-flags

    fDebug = GetBoolArg("-debug");

    // -debug implies fDebug*
    if (fDebug)
    {
        fDebugNet  = true;
        // maybe secure messaging some day
        // fDebugSmsg = true;
    } else
    {
        fDebugNet  = GetBoolArg("-debugnet");
        fDebugSmsg = GetBoolArg("-debugsmsg");
    }
    fNoSmsg = GetBoolArg("-nosmsg");
    
    bitdb.SetDetach(GetBoolArg("-detachdb", false));

#if !defined(WIN32) && !defined(QT_GUI)
    fDaemon = GetBoolArg("-daemon");
#else
    fDaemon = false;
#endif

    if (fDaemon)
        fServer = true;
    else
        fServer = GetBoolArg("-server");

    /* force fServer when running without GUI */
#if !defined(QT_GUI)
    fServer = true;
#endif
    fPrintToConsole = GetBoolArg("-printtoconsole");
    fPrintToDebugger = GetBoolArg("-printtodebugger");
    fLogTimestamps = GetBoolArg("-logtimestamps");

    if (mapArgs.count("-timeout"))
    {
        int nNewTimeout = GetArg("-timeout", 5000);
        if (nNewTimeout > 0 && nNewTimeout < 600000)
            nConnectTimeout = nNewTimeout;
    }

    // default currency
    if (nDefaultCurrency == BREAKOUT_COLOR_NONE)
    {
        // std::string strDefaultCurrency = GetArg("-defaultcurrency", COLOR_TICKER[DEFAULT_COLOR]);
        std::string strDefaultCurrency = GetArg("-defaultcurrency", COLOR_TICKER[BREAKOUT_COLOR_NONE]);
        if (!GetColorFromTicker(strDefaultCurrency, nDefaultCurrency))
        {
               if (mapArgs["-defaultcurrency"].size() == 0)
               {
                   return InitError(_("Please assign ticker for -defaultcurrency=<ticker>"));
               }
               else
               {
                   return InitError(strprintf(_("Invalid ticker for -defaultcurrency=<ticker>: '%s'"),
                                                                    mapArgs["-defaultcurrency"].c_str()));
               }
        }
    }


    // default stake
    if (nNumberOfStakingCurrencies == 1)
    {
        nDefaultStake = DEFAULT_STAKE_COLOR;
    }
    else if ((nDefaultStake == BREAKOUT_COLOR_NONE) && (nNumberOfStakingCurrencies > 1))
    {
        std::string strDefaultStake = GetArg("-defaultstake", COLOR_TICKER[BREAKOUT_COLOR_NONE]);
        if (!GetColorFromTicker(strDefaultStake, nDefaultStake))
        {
            if (mapArgs["-defaultstake"].size() == 0)
            {
               return InitError(_("Please assign ticker for -defaultstake=<ticker>"));
            }
            else
            {
               return InitError(strprintf(_("Invalid ticker for (%s) -defaultstake=<ticker>: '%s'"),
                                     strDefaultStake.c_str(), mapArgs["-defaultstake"].c_str()));
            }
        }
        if (!CanStake(nDefaultStake))
        {
            return InitError(strprintf(_("Currency never stakes for (%s) -defaultstake=<ticker>: '%s'"),
                                              strDefaultStake.c_str(), mapArgs["-defaultstake"].c_str()));
        }
    }


    // meaning of paytxfee: this is the fee for sending the specified currency

    // default tx fee for default currency, either unset or using -defaultcurrency
    if (mapArgs.count("-paytxfee"))
    {
        int nFeeColor = FEE_COLOR[nDefaultCurrency];
        int64_t nTxFee = 0;
        // for sideways compatibility (exchanges, etc)
        if (!ParseMoney(mapArgs["-paytxfee"], nFeeColor, nTxFee))
        {
            InitError(_("Invalid amount for -paytxfee=<amount>"));
            return false;
        }
        vTransactionFee[nFeeColor] = nTxFee;
        if (nTxFee > VERY_HIGH_FEE * MIN_TX_FEE[nFeeColor])
        {
            InitWarning(_("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you   send a transaction."));
        }
    }

    // =========== these should be a loop ======== //
    if (mapArgs.count("-paytxfee_brostake"))
    {
        int nFeeColor = FEE_COLOR[BREAKOUT_COLOR_BROSTAKE];
        int64_t nTxFee = 0;
        if (!ParseMoney(mapArgs["-paytxfee_brostake"], nFeeColor, nTxFee))
        {
            InitError(_("Invalid amount for -paytxfee_brostake=<amount>"));
            return false;
        }
        vTransactionFee[nFeeColor] = nTxFee;
        if (nTxFee > VERY_HIGH_FEE * MIN_TX_FEE[nFeeColor])
        {
            InitWarning(_("Warning: -paytxfee_brostake is set very high! This is the transaction fee you will pay if you send a transaction."));
        }
    }

    if (mapArgs.count("-paytxfee_brocoin"))
    {
        int nFeeColor = FEE_COLOR[BREAKOUT_COLOR_BROCOIN];
        int64_t nTxFee = 0;
        if (!ParseMoney(mapArgs["-paytxfee_brocoin"], nFeeColor, nTxFee))
        {
            InitError(_("Invalid amount for -paytxfee_brocoin=<amount>"));
            return false;
        }
        vTransactionFee[nFeeColor] = nTxFee;
        if (nTxFee > VERY_HIGH_FEE * MIN_TX_FEE[nFeeColor])
        {
            InitWarning(_("Warning: -paytxfee_brocoin is set very high! This is the transaction fee you will pay if you send a transaction."));
        }
    }

    if (mapArgs.count("-paytxfee_sistercoin"))
    {
        int nFeeColor = FEE_COLOR[BREAKOUT_COLOR_SISCOIN];
        int64_t nTxFee = 0;
        if (!ParseMoney(mapArgs["-paytxfee_sistercoin"], nFeeColor, nTxFee))
        {
            InitError(_("Invalid amount for -paytxfee_sistercoin=<amount>"));
            return false;
        }
        vTransactionFee[nFeeColor] = nTxFee;
        if (nTxFee > VERY_HIGH_FEE * MIN_TX_FEE[nFeeColor])
        {
            InitWarning(_("Warning: -paytxfee_brocoin is set very high! This is the transaction fee you will pay if you send a transaction."));
        }
    }

    // =========================================== //

    // by color number
    for (int nColor=1; nColor < N_COLORS; ++nColor)
    {
        int nFeeColor = FEE_COLOR[nColor];
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-paytxfee_%d", nColor);
        int64_t nTxFee = 0;
        if (mapArgs.count(cArg))
        {
            if (!ParseMoney(mapArgs[cArg], nFeeColor, nTxFee))
            {
                InitError(strprintf(_("Invalid amount for %s=<amount>"), cArg));
                return false;
            }
            vTransactionFee[nFeeColor] = nTxFee;
        }
        if (nTxFee > VERY_HIGH_FEE * MIN_TX_FEE[nFeeColor])
        {
            char msg[120];
            snprintf(msg, sizeof(msg), "Warning: -paytxfee_%d is set very high! This is the transaction fee you will pay if you send a transaction.", nColor);
            InitWarning(_(msg));
        }
    }

    // by ticker
    for (int nColor=1; nColor < N_COLORS; ++nColor)
    {
        int nFeeColor = FEE_COLOR[nColor];
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-paytxfee_%s", COLOR_TICKER[nColor]);
        int64_t nTxFee = 0;
        if (mapArgs.count(cArg))
        {
            if (!ParseMoney(mapArgs[cArg], nFeeColor, nTxFee))
            {
                InitError(strprintf(_("Invalid amount for %s=<amount>"), cArg));
                return false;
            }
            vTransactionFee[nFeeColor] = nTxFee;
        }
        if (nTxFee > VERY_HIGH_FEE * MIN_TX_FEE[nFeeColor])
        {
            char msg[120];
            snprintf(msg, sizeof(msg), "Warning: -paytxfee_%s is set very high! This is the transaction fee you will pay if you send a transaction.", COLOR_TICKER[nColor]);
            InitWarning(_(msg));
        }
    }



    fConfChange = GetBoolArg("-confchange", false);
    fEnforceCanonical = GetBoolArg("-enforcecanonical", true);



    // default minimum input for default currency either unset or using -defaultcurrency
    if (mapArgs.count("-mininput"))
    {
        int64_t nMinInput;
        // for sideways compatibility (exchanges, etc)
        if (!ParseMoney(mapArgs["-mininput"], nDefaultCurrency, nMinInput))
        {
            InitError(_("Invalid amount for -mininput=<amount>"));
            return false;
        }
        vMinimumInputValue[nDefaultCurrency] = nMinInput;
    }

    // =========== these should be a loop ======== //
    if (mapArgs.count("-mininput_brostake"))
    {
        int64_t nMinInput;
        if (!ParseMoney(mapArgs["-mininput_brostake"], BREAKOUT_COLOR_BROSTAKE, nMinInput))
        {
            InitError(_("Invalid amount for -mininput_brostake=<amount>"));
            return false;
        }
        vMinimumInputValue[BREAKOUT_COLOR_BROSTAKE] = nMinInput;
    }

    if (mapArgs.count("-mininput_brocoin"))
    {
        int64_t nMinInput;
        if (!ParseMoney(mapArgs["-mininput_brocoin"], BREAKOUT_COLOR_BROCOIN, nMinInput))
        {
            InitError(_("Invalid amount for -mininput_brocoin=<amount>"));
            return false;
        }
        vMinimumInputValue[BREAKOUT_COLOR_BROCOIN] = nMinInput;
    }

    if (mapArgs.count("-mininput_sistercoin"))
    {
        int64_t nMinInput;
        if (!ParseMoney(mapArgs["-mininput_sistercoin"], BREAKOUT_COLOR_SISCOIN, nMinInput))
        {
            InitError(_("Invalid amount for -mininput_sistercoin=<amount>"));
            return false;
        }
        vMinimumInputValue[BREAKOUT_COLOR_SISCOIN] = nMinInput;
    }

    // =========================================== //

    // by color number
    for (int nColor=1; nColor < N_COLORS; ++nColor)
    {
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-mininput_%d", nColor);
        if (mapArgs.count(cArg))
        {
            int64_t nMinInput;
            if (!ParseMoney(mapArgs[cArg], nColor, nMinInput))
            {
                InitError(strprintf(_("Invalid amount for %s=<amount>"), cArg));
                return false;
            }
            vMinimumInputValue[nColor] = nMinInput;
        }
    }

    // by ticker
    for (int nColor=1; nColor < N_COLORS; ++nColor)
    {
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-mininput_%s", COLOR_TICKER[nColor]);
        if (mapArgs.count(cArg))
        {
            int64_t nMinInput;
            if (!ParseMoney(mapArgs[cArg], nColor, nMinInput))
            {
                InitError(strprintf(_("Invalid amount for %s=<amount>"), cArg));
                return false;
            }
            vMinimumInputValue[nColor] = nMinInput;
        }
    }


    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    std::string strDataDir = GetDataDir().string();
    std::string strWalletFileName = GetArg("-wallet", "wallet.dat");

    // strWalletFileName must be a plain filename without a directory
    if (strWalletFileName != boost::filesystem::basename(strWalletFileName) + boost::filesystem::extension(strWalletFileName))
        return InitError(strprintf(_("Wallet %s resides outside data directory %s."), strWalletFileName.c_str(), strDataDir.c_str()));

    // Make sure only a single Bitcoin process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
    if (!lock.try_lock())
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s.  breakout is probably already running."), strDataDir.c_str()));

#if !defined(WIN32) && !defined(QT_GUI)
    if (fDaemon)
    {
        // Daemonize
        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
            return false;
        }
        if (pid > 0)
        {
            CreatePidFile(GetPidFile(), pid);
            return true;
        }

        pid_t sid = setsid();
        if (sid < 0)
            fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
    }
#endif

    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();
    printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    printf("breakout version %s (%s)\n", FormatFullVersion().c_str(), CLIENT_DATE.c_str());
    printf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
    if (!fLogTimestamps)
        printf("Startup time: %s\n", DateTimeStrFormat("%x %H:%M:%S", GetTime()).c_str());
    printf("Default data directory %s\n", GetDefaultDataDir().string().c_str());
    printf("Used data directory %s\n", strDataDir.c_str());
    std::ostringstream strErrors;

    if (fDaemon)
        fprintf(stdout, "breakout server starting\n");

    int64_t nStart;

    // ********************************************************* Step 5: verify database integrity

    uiInterface.InitMessage(_("Verifying database integrity..."));

    if (!bitdb.Open(GetDataDir()))
    {
        string msg = strprintf(_("Error initializing database environment %s!"
                                 " To recover, BACKUP THAT DIRECTORY, then remove"
                                 " everything from it except for wallet.dat."), strDataDir.c_str());
        return InitError(msg);
    }

    if (GetBoolArg("-salvagewallet"))
    {
        // Recover readable keypairs:
        if (!CWalletDB::Recover(bitdb, strWalletFileName, true))
            return false;
    }

    if (filesystem::exists(GetDataDir() / strWalletFileName))
    {
        CDBEnv::VerifyResult r = bitdb.Verify(strWalletFileName, CWalletDB::Recover);
        if (r == CDBEnv::RECOVER_OK)
        {
            string msg = strprintf(_("Warning: wallet.dat corrupt, data salvaged!"
                                     " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                     " your balance or transactions are incorrect you should"
                                     " restore from a backup."), strDataDir.c_str());
            uiInterface.ThreadSafeMessageBox(msg, _("breakout"), CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION | CClientUIInterface::MODAL);
        }
        if (r == CDBEnv::RECOVER_FAIL)
            return InitError(_("wallet.dat corrupt, salvage failed"));
    }


    // ********************************************************* Step 6: network initialization

    // uiInterface.InitMessage(_("Initializing network..."));
    int nSocksVersion = GetArg("-socks", 5);

    if (nSocksVersion != 4 && nSocksVersion != 5)
        return InitError(strprintf(_("Unknown -socks proxy version requested: %d"), nSocksVersion));

    // built-in tor is enabled by default (but set to true elsewhere)
    bool fBuiltinTor = false;
    bool fExternalTor = false;

    // -onlynet indicates a specific network selection and not to use built in TOR
    std::set<enum Network> setNets;
    if (mapArgs.count("-onlynet"))
    {
        BOOST_FOREACH(std::string snet, mapMultiArgs["-onlynet"]) {
            // torext doesn't map to unique network, so do this manually
            if (snet == "torext")
            {
                setNets.insert(NET_TOR);
                fExternalTor = true;
            }
            else
            {
                enum Network net = ParseNetwork(snet);
                if (net == NET_UNROUTABLE)
                {
                    return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet.c_str()));
                }
                setNets.insert(net);
                if (net == NET_TOR)
                {
                    fBuiltinTor = true;
                }
            }
        }
    }
    else   // default to built-in tor
    {
        setNets.insert(
            NET_TOR
        );
        fBuiltinTor = true;
    }
    for (int n = 0; n < NET_MAX; n++) {
        enum Network net = (enum Network)n;
        if (!setNets.count(net))
            SetLimited(net);
    }

#if defined(USE_IPV6)
#if ! USE_IPV6
    for (std::set<enum Network>::iterator it = setNets.begin(); it != setNets.end(); ++it)
    {
        if (*it == NET_IPV6)
        {
            setNets.erase(it);
        }
    }
    SetLimited(NET_IPV6);
#endif
#endif

    CService addrProxy;
    bool fProxy = false;
    if (mapArgs.count("-proxy")) {
        addrProxy = CService(mapArgs["-proxy"], GetDefaultProxy());
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address: '%s'"), mapArgs["-proxy"].c_str()));

        if (!IsLimited(NET_IPV4))
            SetProxy(NET_IPV4, addrProxy, nSocksVersion);
        if (nSocksVersion > 4) {
#ifdef USE_IPV6
            if (!IsLimited(NET_IPV6))
                SetProxy(NET_IPV6, addrProxy, nSocksVersion);
#endif
            SetNameProxy(addrProxy, nSocksVersion);
        }
        fProxy = true;
    }

// if not using built in tor, check for external
// this test means that internal and external tor are exclusive
if (fBuiltinTor)
{
    CService addrOnion;
    p2p_port = GetDefaultPort();
    onion_port = (unsigned short)GetArg("-torport", TOR_PORT);
    if (mapArgs.count("-tor") && mapArgs["-tor"] != "0") {
        addrOnion = CService(mapArgs["-tor"], onion_port);
        if (!addrOnion.IsValid())
            return InitError(strprintf(_("Invalid -tor address: '%s'"), mapArgs["-tor"].c_str()));
    } else {
        addrOnion = CService("127.0.0.1", onion_port);
    }
    SetProxy(NET_TOR, addrOnion, 5);
    SetReachable(NET_TOR);
}
else
{
    // -torext can override normal proxy, -notorext disables tor entirely
    if (!(mapArgs.count("-torext") && mapArgs["-torext"] == "0") &&
        (fProxy || mapArgs.count("-torext") || fExternalTor)) {
        CService addrOnion;
        if (!mapArgs.count("-torext"))
        {
            if (fExternalTor)
            {
                return InitError(std::string("Specify -torext address (e.g. '127.0.0.1:9150')"));
            }
            else
            {
                addrOnion = addrProxy;
            }
        }
        else
        {
            addrOnion = CService(mapArgs["-torext"], 9050);
        }
        if (!addrOnion.IsValid())
        {
            return InitError(strprintf(_("Invalid -torext address: '%s'"), mapArgs["-torext"].c_str()));
        }
        SetProxy(NET_TOR, addrOnion);
        SetReachable(NET_TOR);
    }
}


    // see Step 2: parameter interactions for more information about these
    fNoListen = !GetBoolArg("-listen", true);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", true);

    bool fBound = false;

    if (!fNoListen)
    {
        // TODO: use of -bind is not fully tested
        std::string strError;
        if (mapArgs.count("-bind"))
        {
            BOOST_FOREACH(std::string strBind, mapMultiArgs["-bind"])
            {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind.c_str()));
                fBound |= Bind(addrBind);
            }
        } else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
#ifdef USE_IPV6
            if (!IsLimited(NET_IPV6))
                fBound |= Bind(CService(in6addr_any, GetListenPort()), false);
#endif
            if (!IsLimited(NET_IPV4))
            {
                fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound);
            }
        }

        // in any case try to bind to 127.0.0.1 if using builtin tor
        if (fBuiltinTor || fExternalTor)
        {
            CService addrBind;
            if (!Lookup("127.0.0.1", addrBind, GetListenPort(), false))
            {
                return InitError(strprintf(_("Cannot resolve binding address: '%s'"),  "127.0.0.1"));
            }
            fBound |= Bind(addrBind);
        }

        if (!fBound)
        {
            return InitError(_("Failed to listen on any port."));
        }
    }

    if (fBuiltinTor)
    {
        // uiInterface.InitMessage(_("Starting Tor..."));

        // start up tor
        if (!(mapArgs.count("-tor") && mapArgs["-tor"] != "0")) {
          if (!NewThread(StartTor, NULL))
            InitError(_("Error: could not start tor"));
        }

        // uiInterface.InitMessage(_("Waiting for Tor initialization..."));
        wait_initialized();
        uiInterface.InitMessage(_("Tor Initialized."));
    }


    if (mapArgs.count("-externalip"))
    {
        BOOST_FOREACH(string strAddr, mapMultiArgs["-externalip"]) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr.c_str()));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }
    else if (fBuiltinTor)
    {
        string automatic_onion;
        filesystem::path const hostname_path = GetDataDir(
        ) / "onion" / "hostname";
        if (
            !filesystem::exists(
                hostname_path
            )
        ) {
            return InitError(strprintf(_("No external address found. %s"), hostname_path.string().c_str()));
        }
        ifstream file(
            hostname_path.string(
            ).c_str(
            )
        );
        file >> automatic_onion;
        AddLocal(CService(automatic_onion, GetListenPort(), fNameLookup), LOCAL_MANUAL);
    }

    BOOST_FOREACH(string strDest, mapMultiArgs["-seednode"])
        AddOneShot(strDest);


/* Reserve Balances */

    // default reserve balance for coins with a primary staking currency
    // and/or -defaultstake set
    if (mapArgs.count("-reservebalance"))
    {
        int64_t nReserveBalance;
        // for sideways compatibility (exchanges, etc)
        if (!ParseMoney(mapArgs["-reservebalance"], nDefaultStake, nReserveBalance))
        {
            InitError(_("Invalid amount for -reservebalance=<amount>"));
            return false;
        }
        vReserveBalance[nDefaultStake] = nReserveBalance;
    }

    // =========== these should be a loop (only 1 here) ======== //

    if (mapArgs.count("-reservebalance_brostake"))
    {
        int64_t nReserveBalance;
        if (!ParseMoney(mapArgs["-reservebalance_brostake"], BREAKOUT_COLOR_BROSTAKE, nReserveBalance))
        {
            InitError(_("Invalid amount for -reservebalance_brostake=<amount>"));
            return false;
        }
        vReserveBalance[BREAKOUT_COLOR_BROSTAKE] = nReserveBalance;
    }
    // =========================================== //

    // by color number
    for (int nColor=1; nColor < N_COLORS; ++nColor)
    {
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-reservebalance_%d", nColor);
        int64_t nReserveBalance;
        if (mapArgs.count(cArg))
        {
            if (!ParseMoney(mapArgs[cArg], nColor, nReserveBalance))
            {
                InitError(strprintf(_("Invalid amount for %s=<amount>"), cArg));
                return false;
            }
            vReserveBalance[nColor] = nReserveBalance;
        }
    }

    // by ticker
    for (int nColor=1; nColor < N_COLORS; ++nColor)
    {
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-reservebalance_%s", COLOR_TICKER[nColor]);
        int64_t nReserveBalance;
        if (mapArgs.count(cArg))
        {
            if (!ParseMoney(mapArgs[cArg], nColor, nReserveBalance))
            {
                InitError(strprintf(_("Invalid amount for %s=<amount>"), cArg));
                return false;
            }
            vReserveBalance[nColor] = nReserveBalance;
        }
    }


    if (mapArgs.count("-checkpointkey")) // ppcoin: checkpoint master priv key
    {
        if (!Checkpoints::SetCheckpointPrivKey(GetArg("-checkpointkey", "")))
            InitError(_("Unable to sign checkpoint, wrong checkpointkey?\n"));
    }


    // TODO: replace this by DNSseed
    // AddOneShot(string(""));

    // ********************************************************* Step 7: load blockchain

    if (!bitdb.Open(GetDataDir()))
    {
        string msg = strprintf(_("Error initializing database environment %s!"
                                 " To recover, BACKUP THAT DIRECTORY, then remove"
                                 " everything from it except for wallet.dat."), strDataDir.c_str());
        return InitError(msg);
    }

    if (GetBoolArg("-loadblockindextest"))
    {
        CTxDB txdb("r");
        txdb.LoadBlockIndex();
        PrintBlockTree();
        return false;
    }

    printf("Loading block index...\n");
    uiInterface.InitMessage(_("Loading block index..."));
    nStart = GetTimeMillis();
    if (!LoadBlockIndex())
        return InitError(_("Error loading blkindex.dat"));

    printf("Block index loaded successfully.\n");

    // as LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill bitcoin-qt during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        printf("Shutdown requested. Exiting.\n");
        return false;
    }
    printf(" block index %15" PRId64 "ms\n", GetTimeMillis() - nStart);

    if (GetBoolArg("-printblockindex") || GetBoolArg("-printblocktree"))
    {
        PrintBlockTree();
        return false;
    }

    if (mapArgs.count("-printblock"))
    {
        string strMatch = mapArgs["-printblock"];
        int nFound = 0;
        for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
        {
            uint256 hash = (*mi).first;
            if (strncmp(hash.ToString().c_str(), strMatch.c_str(), strMatch.size()) == 0)
            {
                CBlockIndex* pindex = (*mi).second;
                CBlock block;
                block.ReadFromDisk(pindex);
                block.BuildMerkleTree();
                block.print();
                printf("\n");
                nFound++;
            }
        }
        if (nFound == 0)
            printf("No blocks matching %s were found\n", strMatch.c_str());
        return false;
    }

    // ********************************************************* Step 8: load wallet

    uiInterface.InitMessage(_("Loading wallet..."));
    printf("Loading wallet...\n");
    nStart = GetTimeMillis();
    bool fFirstRun = true;
    pwalletMain = new CWallet(strWalletFileName);
    DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
            strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            string msg(_("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                         " or address book entries might be missing or incorrect."));
            uiInterface.ThreadSafeMessageBox(msg, _("breakout"), CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION | CClientUIInterface::MODAL);
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
        {
            strErrors << _("Error loading wallet.dat: Wallet requires newer version of breakout") << "\n";
        }
        else if (nLoadWalletRet == DB_NEED_REWRITE)
        {
            strErrors << _("Wallet needed to be rewritten: restart breakout to complete") << "\n";
            printf("%s", strErrors.str().c_str());
            return InitError(strErrors.str());
        }
        else
        {
            strErrors << _("Error loading wallet.dat") << "\n";
        }
    }

    if (GetBoolArg("-upgradewallet", fFirstRun))
    {
        int nMaxVersion = GetArg("-upgradewallet", 0);
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            printf("Performing wallet upgrade to %d\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            printf("Allowing wallet upgrade up to %d\n", nMaxVersion);
        if (nMaxVersion < pwalletMain->GetVersion())
            strErrors << _("Cannot downgrade wallet") << "\n";
        pwalletMain->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        // Create new keyUser and set as default key
        RandAddSeedPerfmon();

        CPubKey newDefaultKey;
        if (!pwalletMain->GetKeyFromPool(newDefaultKey, false))
            strErrors << _("Cannot initialize keypool") << "\n";
        newDefaultKey.nColor = nDefaultCurrency;
        pwalletMain->SetDefaultKey(newDefaultKey);
        if (!pwalletMain->SetAddressBookName(pwalletMain->vchDefaultKey.GetID(),
                                                                   nDefaultCurrency, ""))
            strErrors << _("Cannot write default address") << "\n";
    }

    printf("%s", strErrors.str().c_str());
    printf(" wallet      %15" PRId64 "ms\n", GetTimeMillis() - nStart);

    RegisterWallet(pwalletMain);

    CBlockIndex *pindexRescan = pindexBest;
    if (GetBoolArg("-rescan"))
        pindexRescan = pindexGenesisBlock;
    else
    {
        CWalletDB walletdb(strWalletFileName);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = locator.GetBlockIndex();
    }
    if (pindexBest != pindexRescan && pindexBest && pindexRescan && pindexBest->nHeight > pindexRescan->nHeight)
    {
        uiInterface.InitMessage(_("Rescanning..."));
        printf("Rescanning last %d blocks (from block %d)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        pwalletMain->ScanForWalletTransactions(pindexRescan, true);
        printf(" rescan      %15" PRId64 "ms\n", GetTimeMillis() - nStart);
    }

    // ********************************************************* Step 9: import blocks

    if (mapArgs.count("-loadblock"))
    {
        uiInterface.InitMessage(_("Importing blockchain data file."));

        BOOST_FOREACH(string strFile, mapMultiArgs["-loadblock"])
        {
            FILE *file = fopen(strFile.c_str(), "rb");
            if (file)
                LoadExternalBlockFile(file);
        }
        exit(0);
    }

    filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (filesystem::exists(pathBootstrap)) {
        uiInterface.InitMessage(_("Importing bootstrap blockchain data file."));

        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        }
    }

    // ********************************************************* Step 10: load peers

    uiInterface.InitMessage(_("Loading addresses..."));
    printf("Loading addresses...\n");
    nStart = GetTimeMillis();

    {
        CAddrDB adb;
        if (!adb.Read(addrman))
            printf("Invalid or missing peers.dat; recreating\n");
    }

    printf("Loaded %d addresses from peers.dat  %" PRId64 "ms\n",
           addrman.size(), GetTimeMillis() - nStart);

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return false;

    RandAddSeedPerfmon();

    //// debug print
    printf("mapBlockIndex.size() = %"PRIszu"\n",   mapBlockIndex.size());
    printf("nBestHeight = %d\n",            nBestHeight);
    printf("setKeyPool.size() = %"PRIszu"\n",      pwalletMain->setKeyPool.size());
    printf("mapWallet.size() = %"PRIszu"\n",       pwalletMain->mapWallet.size());
    printf("mapAddressBook.size() = %"PRIszu"\n",  pwalletMain->mapAddressBook.size());

    if (!NewThread(StartNode, NULL))
        InitError(_("Error: could not start node"));

    if (fServer)
        NewThread(ThreadRPCServer, NULL);

    // ********************************************************* Step 12: finished

    uiInterface.InitMessage(_("Done loading"));
    printf("Done loading\n");

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

     // Add wallet transactions that aren't already in a block to mapTransactions
    pwalletMain->ReacceptWalletTransactions();

#if !defined(QT_GUI)
    // Loop until process is exit()ed from shutdown() function,
    // called from ThreadRPCServer thread when a "stop" command is received.
    while (1)
        MilliSleep(5000);
#endif

    return true;
}
