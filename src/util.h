// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#include "colors.h"
#include "uint256.h"

#ifndef WIN32
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include <map>
#include <vector>
#include <string>
#include <stdexcept>

#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>


#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

#include "netbase.h" // for AddTimeData

// to obtain PRId64 on some old systems
#define __STDC_FORMAT_MACROS 1

#include <stdint.h>
#include <inttypes.h>

#include <chrono>
#include <thread>


#if __cplusplus >= 201103L
    #define AUTO_PTR std::unique_ptr
#else
    #define AUTO_PTR std::auto_ptr
#endif


#define BEGIN(a)            ((char*)&(a))
#define END(a)              ((char*)&((&(a))[1]))
#define UBEGIN(a)           ((unsigned char*)&(a))
#define UEND(a)             ((unsigned char*)&((&(a))[1]))
#define ARRAYLEN(array)     (sizeof(array)/sizeof((array)[0]))

#define UVOIDBEGIN(a)        ((void*)&(a))
#define CVOIDBEGIN(a)        ((const void*)&(a))
#define UINTBEGIN(a)        ((uint32_t*)&(a))
#define CUINTBEGIN(a)        ((const uint32_t*)&(a))

#ifndef THROW_WITH_STACKTRACE
#define THROW_WITH_STACKTRACE(exception)  \
{                                         \
    LogStackTrace();                      \
    throw (exception);                    \
}
void LogStackTrace();
#endif


/* Format characters for (s)size_t and ptrdiff_t */
#if defined(_MSC_VER) || defined(__MSVCRT__)
  /* (s)size_t and ptrdiff_t have the same size specifier in MSVC:
     http://msdn.microsoft.com/en-us/library/tcxf1dw6%28v=vs.100%29.aspx
   */
  #define PRIszx    "Ix"
  #define PRIszu    "Iu"
  #define PRIszd    "Id"
  #define PRIpdx    "Ix"
  #define PRIpdu    "Iu"
  #define PRIpdd    "Id"
#else /* C99 standard */
  #define PRIszx    "zx"
  #define PRIszu    "zu"
  #define PRIszd    "zd"
  #define PRIpdx    "tx"
  #define PRIpdu    "tu"
  #define PRIpdd    "td"
#endif

#ifdef PRId64
#if defined(_MSC_VER) || defined(__MSVCRT__)
#undef PRId64
#define PRId64 "I64d"
#undef PRIu64
#define PRIu64 "I64u"
#undef PRIx64
#define PRIx64 "I64x"
#endif
#endif

// This is needed because the foreach macro can't get over the comma in pair<t1, t2>
#define PAIRTYPE(t1, t2)    std::pair<t1, t2>

// Align by increasing pointer, must have extra space at end of buffer
template <size_t nBytes, typename T>
T* alignup(T* p)
{
    union
    {
        T* ptr;
        size_t n;
    } u;
    u.ptr = p;
    u.n = (u.n + (nBytes-1)) & ~(nBytes-1);
    return u.ptr;
}

#ifdef WIN32
#define MSG_NOSIGNAL        0
#define MSG_DONTWAIT        0

#ifndef S_IRUSR
#define S_IRUSR             0400
#define S_IWUSR             0200
#endif
#else
#define MAX_PATH            1024
#endif

inline void MilliSleep(int64_t n)
{
#if defined(__APPLE__)
    // Apple silicon (arm64) pointer authentication and MacOS in
    //    general seems to have issues with boost library thread
    //    sleeping, so we will use standard library.
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
#elif BOOST_VERSION < 105000
    boost::this_thread::sleep(boost::posix_time::milliseconds(n));
#else
    boost::this_thread::sleep_for(boost::chrono::milliseconds(n));
#endif
}

// sleeps in steps of nGrainSize, checking for fShutdown every step
void GranularMilliSleep(int64_t n, int64_t nGrainSize);

/* This GNU C extension enables the compiler to check the format string against the parameters provided.
 * X is the number of the "format string" parameter, and Y is the number of the first variadic parameter.
 * Parameters count from 1.
 */
#ifdef __GNUC__
#define ATTR_WARN_PRINTF(X,Y) __attribute__((format(printf,X,Y)))
#else
#define ATTR_WARN_PRINTF(X,Y)
#endif

class CValidationState;

extern CValidationState validationStateMain;
extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;
extern bool fDebug;
extern bool fDebugNet;
extern bool fDebugMiner;
// maybe secure messaging some day
extern bool fDebugSmsg;
extern bool fNoSmsg;
extern bool fPrintToConsole;
extern bool fPrintToDebugger;
extern bool fRequestShutdown;
extern bool fShutdown;
extern bool fDaemon;
extern bool fServer;
extern bool fCommandLine;
extern std::string strMiscWarning;
extern bool fTestNet;
extern bool fNoListen;
extern bool fLogTimestamps;
extern bool fReopenDebugLog;

extern int nMaxHeight;

void RandAddSeed();
void RandAddSeedPerfmon();
int ATTR_WARN_PRINTF(1,2) OutputDebugStringF(const char* pszFormat, ...);

/*
  Rationale for the real_strprintf / strprintf construction:
    It is not allowed to use va_start with a pass-by-reference argument.
    (C++ standard, 18.7, paragraph 3). Use a dummy argument to work around this, and use a
    macro to keep similar semantics.
*/

/** Overload strprintf for char*, so that GCC format type warnings can be given */
std::string ATTR_WARN_PRINTF(1,3) real_strprintf(const char *format, int dummy, ...);
/** Overload strprintf for std::string, to be able to use it with _ (translation).
 * This will not support GCC format type warnings (-Wformat) so be careful.
 */
std::string real_strprintf(const std::string &format, int dummy, ...);
#define strprintf(format, ...) real_strprintf(format, 0, __VA_ARGS__)
std::string vstrprintf(const char *format, va_list ap);

bool ATTR_WARN_PRINTF(1,2) error(const char *format, ...);

/* Redefine printf so that it directs output to debug.log
 *
 * Do this *after* defining the other printf-like functions, because otherwise the
 * __attribute__((format(printf,X,Y))) gets expanded to __attribute__((format(OutputDebugStringF,X,Y)))
 * which confuses gcc.
 */
#define printf OutputDebugStringF

/**
 * Return the number of physical cores available on the current system.
 * @note This does not count virtual cores, such as those provided by HyperThreading
 * when boost is newer than 1.56.
 */
int GetNumCores();

void LogException(std::exception* pex, const char* pszThread);
void PrintException(std::exception* pex, const char* pszThread);
void PrintExceptionContinue(std::exception* pex, const char* pszThread);
bool ParseUInt64(const std::string& str, int base, uint64_t* nRet);
void ParseString(const std::string& str, char c, std::vector<std::string>& v);
std::string FormatMoney(int64_t n,
                        int nColor,
                        bool fPlus = false,
                        bool fShowTicker = true);
bool ParseMoney(const std::string& str, int nColor, int64_t& nRet);
bool ParseMoney(const char* pszIn, int nColor, int64_t& nRet);
valtype ParseHex(const char* psz);
valtype ParseHex(const std::string& str);
bool IsHex(const std::string& str);
valtype DecodeBase64(const char* p, bool* pfInvalid = NULL);
std::string DecodeBase64(const std::string& str);
std::string EncodeBase64(const unsigned char* pch, size_t len);
std::string EncodeBase64(const std::string& str);
valtype DecodeBase32(const char* p, bool* pfInvalid = NULL);
std::string DecodeBase32(const std::string& str);
std::string EncodeBase32(const unsigned char* pch, size_t len);
std::string EncodeBase32(const std::string& str);
void ParseParameters(int argc, const char*const argv[]);
bool WildcardMatch(const char* psz, const char* mask);
bool WildcardMatch(const std::string& str, const std::string& mask);
void FileCommit(FILE *fileout);
bool RenameOver(boost::filesystem::path src, boost::filesystem::path dest);
boost::filesystem::path GetDefaultDataDir();
const boost::filesystem::path &GetDataDir(bool fNetSpecific = true);
boost::filesystem::path GetConfigFile();
boost::filesystem::path GetPidFile();
#ifndef WIN32
void CreatePidFile(const boost::filesystem::path &path, pid_t pid);
#endif
void ReadConfigFile(std::map<std::string, std::string>& mapSettingsRet, std::map<std::string, std::vector<std::string> >& mapMultiSettingsRet);
#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif
void ShrinkDebugFile();
int GetRandInt(int nMax);
uint64_t GetRand(uint64_t nMax);
uint256 GetRandHash();
int64_t GetTime();
int64_t GetTimeMillis();
int64_t GetTimeMicros();
// Like GetTime(), but not mockable
int64_t GetSystemTimeInSeconds();
void SetMockTime(int64_t nMockTimeIn);
int64_t GetAdjustedTime();
int64_t GetTimeOffset();
std::string FormatFullVersion();
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments);
void AddTimeData(const CNetAddr& ip, int64_t nTime);
void runCommand(std::string strCommand);


class CValidationState
{
private:
    enum mode_state
    {
        MODE_VALID,   //!< everything ok
        MODE_INVALID, //!< network rule violation (DoS value may be set)
        MODE_ERROR,   //!< run-time error
    } mode;

public:
    unsigned int nCode;
    std::string strMessage;
    uint256 hashTx;
    uint256 hashBlock;
    std::string strDebugMessage;

    CValidationState() : mode(MODE_VALID), nCode(0) {}

    const char* GetMode() const
    {
        switch (mode)
        {
        case MODE_VALID: return "valid";
        case MODE_INVALID: return "invalid";
        case MODE_ERROR: return "error";
        }
        return NULL;
    }

    std::string ToString() const
    {
        return strprintf(
            "CValidationState(mode=%s, nCode=%u (%x)\n  message=\"%s\",\n  "
            "hashTx=%s,\n  hashBlock=%s,\n  strDebugMessage=\"%s\")",
            GetMode(),
            nCode,
            nCode,
            strMessage.c_str(),
            hashTx.ToString().c_str(),
            hashBlock.ToString().c_str(),
            strDebugMessage.c_str());
    }
    void Clear()
    {
        mode = MODE_VALID;
        nCode = 0;
        strMessage.clear();
        hashBlock = 0;
        hashTx = 0;
        strDebugMessage.clear();
    }
    bool Set(mode_state modeIn,
             unsigned int nCodeIn,
             std::string strMessageIn,
             uint256 hashTxIn,
             uint256 hashBlockIn,
             std::string strDebugMessageIn)
    {
        mode = modeIn;
        nCode = nCodeIn;
        hashTx = hashTxIn;
        hashBlock = hashBlockIn;
        strDebugMessage = strDebugMessageIn;
        return mode == MODE_VALID;
    }
    bool Valid()
    {
        Clear();
        return true;
    }
    bool Invalid(unsigned int nCodeIn,
                 std::string strMessageIn = "",
                 uint256 hashTxIn = 0,
                 uint256 hashBlockIn = 0,
                 std::string strDebugMessageIn = "")
    {

        nCodeIn = std::max(1u, nCodeIn);
        return Set(MODE_INVALID,
                   nCodeIn,
                   strMessageIn,
                   hashTxIn,
                   hashBlockIn,
                   strDebugMessageIn);
    }
    bool Error(unsigned int nCodeIn,
               std::string strMessageIn = "",
               uint256 hashTxIn = 0,
               uint256 hashBlockIn = 0,
               std::string strDebugMessageIn = "")
    {
        nCodeIn = std::max(1u, nCodeIn);
        return Set(MODE_ERROR,
                   nCodeIn,
                   strMessageIn,
                   hashTxIn,
                   hashBlockIn,
                   strDebugMessageIn);
    }
    bool IsValid() const
    {
        return mode == MODE_VALID;
    }
    bool IsInvalid() const
    {
        return mode == MODE_INVALID;
    }
    bool IsError() const
    {
        return mode == MODE_ERROR;
    }
};




class CProgressHelper
{
private:
    void (*pProgress)(void*, unsigned int);
    void* pContext;
    unsigned int nEvery;
public:
    CProgressHelper()
    {
        pProgress = NULL;
        pContext = NULL;
        nEvery = 0;
    }
    CProgressHelper(void (*pProgressIn)(void*, unsigned int),
                    void* pContextIn,
                    unsigned int nEveryIn)
    {
        pProgress = pProgressIn;
        pContext = pContextIn;
        nEvery = nEveryIn;
    }
    void update(unsigned int pct) const
    {
        if (pProgress)
        {
            pProgress(pContext, std::min(100u, pct));
        }
    }
    void update(unsigned int n, unsigned int total) const
    {
        // no matter if total is 0 or not, update to 0 if n is 0
        // also, avoid divide by 0
        if (n && nEvery)
        {
            if (((n % nEvery) == 0) && total)
            {
                if (n < total)
                {
                    update((100 * n) / total);
                }
                else
                {
                    update(100);
                }
            }
        }
        else
        {
            update(0);
        }
    }
    void setContext(void* pContextIn)
    {
        pContext = pContextIn;
    }
    void setEvery(unsigned int nEveryIn)
    {
       nEvery = nEveryIn;
    }
};

extern const CProgressHelper progressQuiet;
extern void stdErrProgress(void *d, unsigned int v);
extern void stdOutProgress(void *d, unsigned int v);
extern void logProgress(void *d, unsigned int v);

inline std::string i64tostr(int64_t n)
{
    return strprintf("%" PRId64, n);
}

inline std::string itostr(int n)
{
    return strprintf("%d", n);
}

inline int64_t atoi64(const char* psz)
{
#ifdef _MSC_VER
    return _atoi64(psz);
#else
    return strtoll(psz, NULL, 10);
#endif
}

inline int64_t atoi64(const std::string& str)
{
#ifdef _MSC_VER
    return _atoi64(str.c_str());
#else
    return strtoll(str.c_str(), NULL, 10);
#endif
}

inline int atoi(const std::string& str)
{
    return atoi(str.c_str());
}

inline int roundint(double d)
{
    return (int)(d > 0 ? d + 0.5 : d - 0.5);
}

inline int64_t roundint64(double d)
{
    return (int64_t)(d > 0 ? d + 0.5 : d - 0.5);
}

inline int64_t abs64(int64_t n)
{
    return (n >= 0 ? n : -n);
}

inline std::string leftTrim(std::string src, char chr)
{
    std::string::size_type pos = src.find_first_not_of(chr, 0);

    if(pos > 0)
        src.erase(0, pos);

    return src;
}

template<typename T>
void PrintHex(const T pbegin, const T pend, const char* pszFormat="%s", bool fSpaces=true)
{
    printf(pszFormat, HexStr(pbegin, pend, fSpaces).c_str());
}

inline void PrintHex(const valtype& vch, const char* pszFormat="%s", bool fSpaces=true)
{
    printf(pszFormat, HexStr(vch, fSpaces).c_str());
}

inline int64_t GetPerformanceCounter()
{
    int64_t nCounter = 0;
#ifdef WIN32
    QueryPerformanceCounter((LARGE_INTEGER*)&nCounter);
#else
    timeval t;
    gettimeofday(&t, NULL);
    nCounter = (int64_t) t.tv_sec * 1000000 + t.tv_usec;
#endif
    return nCounter;
}

inline std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    time_t n = nTime;
    struct tm* ptmTime = gmtime(&n);
    char pszTime[200];
    strftime(pszTime, sizeof(pszTime), pszFormat, ptmTime);
    return pszTime;
}

static const std::string strTimestampFormat = "%Y-%m-%d %H:%M:%S UTC";
inline std::string DateTimeStrFormat(int64_t nTime)
{
    return DateTimeStrFormat(strTimestampFormat.c_str(), nTime);
}


template<typename T>
void skipspaces(T& it)
{
    while (isspace(*it))
        ++it;
}

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault=false);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);


/**
 * MWC RNG of George Marsaglia
 * This is intended to be fast. It has a period of 2^59.3, though the
 * least significant 16 bits only have a period of about 2^30.1.
 *
 * @return random value
 */
extern uint32_t insecure_rand_Rz;
extern uint32_t insecure_rand_Rw;
static inline uint32_t insecure_rand(void)
{
  insecure_rand_Rz=36969*(insecure_rand_Rz&65535)+(insecure_rand_Rz>>16);
  insecure_rand_Rw=18000*(insecure_rand_Rw&65535)+(insecure_rand_Rw>>16);
  return (insecure_rand_Rw<<16)+insecure_rand_Rz;
}

/**
 * Seed insecure_rand using the random pool.
 * @param Deterministic Use a determinstic seed
 */
void seed_insecure_rand(bool fDeterministic=false);


// RAII wrapper for EVP_MD_CTX to ensure cleanup
struct EvpMdCtx {
    EVP_MD_CTX* ctx;
    EvpMdCtx() : ctx(EVP_MD_CTX_new()) {
        if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    ~EvpMdCtx() { EVP_MD_CTX_free(ctx); }
    // Non-copyable
    EvpMdCtx(const EvpMdCtx&) = delete;
    EvpMdCtx& operator=(const EvpMdCtx&) = delete;
};

// Internal helper: single SHA-256 pass over raw bytes -> fills out[32]
inline void SHA256_Once(const unsigned char* data, size_t len, unsigned char out[32])
{
    EvpMdCtx mdctx;
    unsigned int outlen = 32;
    if (!EVP_DigestInit_ex(mdctx.ctx, EVP_sha256(), nullptr) ||
        !EVP_DigestUpdate(mdctx.ctx, data, len) ||
        !EVP_DigestFinal_ex(mdctx.ctx, out, &outlen))
        throw std::runtime_error("SHA256_Once failed");
}

// Internal helper: single SHA-1 pass over raw bytes -> fills out[20]
inline void SHA1_Once(const unsigned char* data, size_t len, unsigned char out[20])
{
    EvpMdCtx mdctx;
    unsigned int outlen = 20;
    if (!EVP_DigestInit_ex(mdctx.ctx, EVP_sha1(), nullptr) ||
        !EVP_DigestUpdate(mdctx.ctx, data, len) ||
        !EVP_DigestFinal_ex(mdctx.ctx, out, &outlen))
        throw std::runtime_error("SHA1_Once failed");
}

// Double-SHA256 of a single range
template<typename T1>
inline uint256 Hash(const T1 pbegin, const T1 pend)
{
    static const unsigned char pblank[1] = {};
    const unsigned char* data  = (pbegin == pend)
        ? pblank
        : reinterpret_cast<const unsigned char*>(&pbegin[0]);
    size_t len = (pend - pbegin) * sizeof(pbegin[0]);

    uint256 hash1, hash2;
    SHA256_Once(data, len, reinterpret_cast<unsigned char*>(&hash1));
    SHA256_Once(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1),
                reinterpret_cast<unsigned char*>(&hash2));
    return hash2;
}

class CHashWriter
{
private:
    EvpMdCtx mdctx;

public:
    int nType;
    int nVersion;

    CHashWriter(int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn) {
        if (!EVP_DigestInit_ex(mdctx.ctx, EVP_sha256(), nullptr))
            throw std::runtime_error("CHashWriter: EVP_DigestInit_ex failed");
    }

    CHashWriter& write(const char* pch, size_t size) {
        if (!EVP_DigestUpdate(mdctx.ctx, pch, size))
            throw std::runtime_error("CHashWriter: EVP_DigestUpdate failed");
        return *this;
    }

    // Invalidates the object (finalises the context)
    uint256 GetHash() {
        uint256 hash1, hash2;
        unsigned int outlen = 32;
        if (!EVP_DigestFinal_ex(mdctx.ctx,
                                reinterpret_cast<unsigned char*>(&hash1),
                                &outlen))
            throw std::runtime_error("CHashWriter: EVP_DigestFinal_ex failed");
        SHA256_Once(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1),
                    reinterpret_cast<unsigned char*>(&hash2));
        return hash2;
    }

    template<typename T>
    CHashWriter& operator<<(const T& obj) {
        ::Serialize(*this, obj, nType, nVersion);
        return *this;
    }
};

// Internal helper: initialise a fresh context and feed one range into it
inline void ctx_update_range(EVP_MD_CTX* ctx,
                              const unsigned char* data, size_t len)
{
    static const unsigned char pblank[1] = {};
    if (!EVP_DigestUpdate(ctx, len ? data : pblank, len))
        throw std::runtime_error("EVP_DigestUpdate failed");
}

// Double-SHA256 over two ranges
template<typename T1, typename T2>
inline uint256 Hash(const T1 p1begin, const T1 p1end,
                    const T2 p2begin, const T2 p2end)
{
    EvpMdCtx mdctx;
    uint256 hash1, hash2;
    unsigned int outlen = 32;

    if (!EVP_DigestInit_ex(mdctx.ctx, EVP_sha256(), nullptr))
        throw std::runtime_error("Hash(2): EVP_DigestInit_ex failed");

    ctx_update_range(mdctx.ctx,
        reinterpret_cast<const unsigned char*>(p1begin == p1end ? nullptr : &p1begin[0]),
        (p1end - p1begin) * sizeof(p1begin[0]));
    ctx_update_range(mdctx.ctx,
        reinterpret_cast<const unsigned char*>(p2begin == p2end ? nullptr : &p2begin[0]),
        (p2end - p2begin) * sizeof(p2begin[0]));

    if (!EVP_DigestFinal_ex(mdctx.ctx,
                            reinterpret_cast<unsigned char*>(&hash1), &outlen))
        throw std::runtime_error("Hash(2): EVP_DigestFinal_ex failed");

    SHA256_Once(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1),
                reinterpret_cast<unsigned char*>(&hash2));
    return hash2;
}

// Double-SHA256 over three ranges
template<typename T1, typename T2, typename T3>
inline uint256 Hash(const T1 p1begin, const T1 p1end,
                    const T2 p2begin, const T2 p2end,
                    const T3 p3begin, const T3 p3end)
{
    EvpMdCtx mdctx;
    uint256 hash1, hash2;
    unsigned int outlen = 32;

    if (!EVP_DigestInit_ex(mdctx.ctx, EVP_sha256(), nullptr))
        throw std::runtime_error("Hash(3): EVP_DigestInit_ex failed");

    ctx_update_range(mdctx.ctx,
        reinterpret_cast<const unsigned char*>(p1begin == p1end ? nullptr : &p1begin[0]),
        (p1end - p1begin) * sizeof(p1begin[0]));
    ctx_update_range(mdctx.ctx,
        reinterpret_cast<const unsigned char*>(p2begin == p2end ? nullptr : &p2begin[0]),
        (p2end - p2begin) * sizeof(p2begin[0]));
    ctx_update_range(mdctx.ctx,
        reinterpret_cast<const unsigned char*>(p3begin == p3end ? nullptr : &p3begin[0]),
        (p3end - p3begin) * sizeof(p3begin[0]));

    if (!EVP_DigestFinal_ex(mdctx.ctx,
                            reinterpret_cast<unsigned char*>(&hash1), &outlen))
        throw std::runtime_error("Hash(3): EVP_DigestFinal_ex failed");

    SHA256_Once(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1),
                reinterpret_cast<unsigned char*>(&hash2));
    return hash2;
}

template<typename T>
uint256 SerializeHash(const T& obj, int nType=SER_GETHASH, int nVersion=PROTOCOL_VERSION)
{
    CHashWriter ss(nType, nVersion);
    ss << obj;
    return ss.GetHash();
}

// Internal helper: single RIPEMD-160 pass over raw bytes -> fills out[20]
inline void RIPEMD160_Once(const unsigned char* data, size_t len, unsigned char out[20])
{
    EvpMdCtx mdctx;
    unsigned int outlen = 20;
    if (!EVP_DigestInit_ex(mdctx.ctx, EVP_ripemd160(), nullptr) ||
        !EVP_DigestUpdate(mdctx.ctx, data, len) ||
        !EVP_DigestFinal_ex(mdctx.ctx, out, &outlen))
        throw std::runtime_error("RIPEMD160_Once failed");
}

// Hash160 (SHA256 then RIPEMD160) of a valtype
inline uint160 Hash160(const valtype& vch)
{
    uint256 hash1;
    SHA256_Once(vch.data(), vch.size(),
                reinterpret_cast<unsigned char*>(&hash1));

    uint160 hash2;
    RIPEMD160_Once(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1),
                   reinterpret_cast<unsigned char*>(&hash2));
    return hash2;
}

// Hash160 (SHA256 then RIPEMD160) of an iterator range
template<typename T1>
inline uint160 Hash160(const T1 pbegin, const T1 pend)
{
    static const unsigned char pblank[1] = {};
    const unsigned char* data = (pbegin == pend)
        ? pblank
        : reinterpret_cast<const unsigned char*>(&pbegin[0]);
    size_t len = (pend - pbegin) * sizeof(pbegin[0]);

    uint256 hash1;
    SHA256_Once(data, len,
                reinterpret_cast<unsigned char*>(&hash1));

    uint160 hash2;
    RIPEMD160_Once(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1),
                   reinterpret_cast<unsigned char*>(&hash2));
    return hash2;
}

/**
 * Timing-attack-resistant comparison.
 * Takes time proportional to length
 * of first argument.
 */
template <typename T>
bool TimingResistantEqual(const T& a, const T& b)
{
    if (b.size() == 0) return a.size() == 0;
    size_t accumulator = a.size() ^ b.size();
    for (size_t i = 0; i < a.size(); i++)
        accumulator |= a[i] ^ b[i%b.size()];
    return accumulator == 0;
}

/** Median filter over a stream of values.
 * Returns the median of the last N numbers
 */
template <typename T> class CMedianFilter
{
private:
    std::vector<T> vValues;
    std::vector<T> vSorted;
    unsigned int nSize;
public:
    CMedianFilter(unsigned int size, T initial_value):
        nSize(size)
    {
        vValues.reserve(size);
        vValues.push_back(initial_value);
        vSorted = vValues;
    }

    void input(T value)
    {
        if(vValues.size() == nSize)
        {
            vValues.erase(vValues.begin());
        }
        vValues.push_back(value);

        vSorted.resize(vValues.size());
        std::copy(vValues.begin(), vValues.end(), vSorted.begin());
        std::sort(vSorted.begin(), vSorted.end());
    }

    T median() const
    {
        int size = vSorted.size();
        assert(size>0);
        if(size & 1) // Odd number of elements
        {
            return vSorted[size/2];
        }
        else // Even number of elements
        {
            return (vSorted[size/2-1] + vSorted[size/2]) / 2;
        }
    }

    int size() const
    {
        return vValues.size();
    }

    std::vector<T> sorted () const
    {
        return vSorted;
    }
};

bool NewThread(void(*pfn)(void*), void* parg);

#ifdef WIN32
inline void SetThreadPriority(int nPriority)
{
    SetThreadPriority(GetCurrentThread(), nPriority);
}
#else

#define THREAD_PRIORITY_LOWEST          PRIO_MAX
#define THREAD_PRIORITY_BELOW_NORMAL    2
#define THREAD_PRIORITY_NORMAL          0
#define THREAD_PRIORITY_ABOVE_NORMAL    0

inline void SetThreadPriority(int nPriority)
{
    // It's unclear if it's even possible to change thread priorities on Linux,
    // but we really and truly need it for the generation threads.
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif
}

inline void ExitThread(size_t nExitCode)
{
    pthread_exit((void*)nExitCode);
}
#endif

void RenameThread(const char* name);

inline uint32_t ByteReverse(uint32_t value)
{
    value = ((value & 0xFF00FF00) >> 8) | ((value & 0x00FF00FF) << 8);
    return (value<<16) | (value>>16);
}

#endif

