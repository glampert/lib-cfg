
// ================================================================================================
// -*- C++ -*-
// File: cfg.cpp
// Author: Guilherme R. Lampert
// Created on: 09/06/16
// Brief: Lib CFG - A small C++ library for configuration file handling, CVars and Commands.
// ================================================================================================

#include "cfg.hpp"

#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cfloat>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#ifdef CFG_BUILD_UNIX_TERMINAL
    #include <atomic>
    #include <thread>
    #include <ctime>
#endif // CFG_BUILD_UNIX_TERMINAL

// ========================================================
// Configurable library macros:
// ========================================================

//
// CVar name comparisons/lookup are case-sensitive if this is defined.
// True if not user-defined, rendering CVar names case-sensitive.
//
#ifndef CFG_CVAR_CASE_SENSITIVE_NAMES
    #define CFG_CVAR_CASE_SENSITIVE_NAMES 1
#endif // CFG_CVAR_CASE_SENSITIVE_NAMES

//
// CVar strings such as the allowed values and boolean
// values are case-sensitive if this is defined.
//
#ifndef CFG_CVAR_CASE_SENSITIVE_STRINGS
    #define CFG_CVAR_CASE_SENSITIVE_STRINGS 1
#endif // CFG_CVAR_CASE_SENSITIVE_STRINGS

//
// Command name comparisons/lookup are case-sensitive if this is defined to nonzero.
// Zero if not user-defined, rendering command names case-insensitive.
//
#ifndef CFG_COMMAND_CASE_SENSITIVE_NAMES
    #define CFG_COMMAND_CASE_SENSITIVE_NAMES 0
#endif // CFG_COMMAND_CASE_SENSITIVE_NAMES

//
// If defined to nonzero, CVar name expansion/substitution is performed
// on command arguments, e.g., expand the var value in a command such as:
// "echo $(my_var)"
//
#ifndef CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION
    #define CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION 1
#endif // CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION

//
// If defined to nonzero, #pragma packs some of
// the base classes so avoid additional padding.
// Disable this is '#pragma pack()' is not supported
// on your target compiler.
//
#ifndef CFG_PACK_STRUCTURES
    #define CFG_PACK_STRUCTURES 1
#endif // CFG_PACK_STRUCTURES

//
// If nonzero, the color::something() functions will return an
// ANSI escape sequence with a color code, otherwise an empty string.
//
#ifndef CFG_USE_ANSI_COLOR_CODES
    #define CFG_USE_ANSI_COLOR_CODES 1
#endif // CFG_USE_ANSI_COLOR_CODES

//
// File name where to save/load the SimpleCommandTerminal command history.
// If this uses a path other than the CWD, the path must already exist.
//
#ifndef CFG_COMMAND_HIST_FILE
    #define CFG_COMMAND_HIST_FILE "cmdhist.txt"
#endif // CFG_COMMAND_HIST_FILE

//
// If no filename is provided to the saveConfig/reloadConfig commands,
// this filename is used. Assumes the CWD. No paths will be created.
//
#ifndef CFG_DEFAULT_CONFIG_FILE
    #define CFG_DEFAULT_CONFIG_FILE "default.cfg"
#endif // CFG_DEFAULT_CONFIG_FILE

//
// Compatibility macros and includes for isatty() and friends.
//
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    #include <unistd.h>        // isatty()
    #include <termios.h>       // tcsetattr/tcgetattr
    #define CFG_ISATTY(fh)     isatty(fh)
    #define CFG_STDIN_FILENO   STDIN_FILENO
    #define CFG_STDOUT_FILENO  STDOUT_FILENO
    #define CFG_STDERR_FILENO  STDERR_FILENO
#elif defined(_WIN32) || defined(_MSC_VER)
    #include <io.h>
    #include <conio.h>
    #define CFG_ISATTY(fh)     _isatty(fh)
    #define CFG_STDIN_FILENO   _fileno(stdin)
    #define CFG_STDOUT_FILENO  _fileno(stdout)
    #define CFG_STDERR_FILENO  _fileno(stderr)
#endif // Apple/Win/Linux

namespace cfg
{

// ================================================================================================
//
//                                Internal library utilities
//
// ================================================================================================

// ========================================================
// Array utility functions:
// ========================================================

// Length in elements of fixed-size C-style arrays.
template<typename T, int Size>
constexpr int lengthOfArray(const T (&)[Size]) noexcept
{
    return Size;
}

// Zero fills a statically allocated array of POD or built-in types.
// Array length inferred by the compiler.
template<typename T, int Size>
static inline void clearArray(T (&array)[Size]) noexcept
{
    static_assert(std::is_pod<T>::value, "Type must be Plain Old Data (POD)!");

    std::memset(array, 0, sizeof(T) * Size);
}

// Zero fills an array of POD or built-in types,
// with array length provided by the caller.
template<typename T>
static inline void clearArray(T * arrayPtr, const int size)
{
    static_assert(std::is_pod<T>::value, "Type must be Plain Old Data (POD)!");

    CFG_ASSERT(arrayPtr != nullptr);
    CFG_ASSERT(size     != 0);

    std::memset(arrayPtr, 0, sizeof(T) * size);
}

// ========================================================
// Library error handler:
// ========================================================

static void defaultErrorHandlerCb(const char * const message, void * /* userContext */)
{
    std::cerr << color::red() << message << color::restore() << std::endl;
}

static ErrorHandlerCallback g_errorHandler = &defaultErrorHandlerCb;
static void *               g_errorUserCtx = nullptr;
static bool                 g_silentErrors = false;

bool errorF(const char * const fmt, ...)
{
    if (g_silentErrors || fmt == nullptr)
    {
        return false;
    }

    va_list vaList;
    char tempStr[2048];

    va_start(vaList, fmt);
    const int result = std::vsnprintf(tempStr, lengthOfArray(tempStr), fmt, vaList);
    va_end(vaList);

    if (result > 0)
    {
        g_errorHandler(tempStr, g_errorUserCtx);
    }

    // Always returns false, so we can write "return errorF(...);"
    return false;
}

void setErrorCallback(ErrorHandlerCallback errorHandler, void * userContext) noexcept
{
    if (errorHandler == nullptr) // Use null to restore the default.
    {
        g_errorHandler = &defaultErrorHandlerCb;
        g_errorUserCtx = nullptr;
    }
    else
    {
        g_errorHandler = errorHandler;
        g_errorUserCtx = userContext;
    }
}

ErrorHandlerCallback getErrorCallback() noexcept
{
    return g_errorHandler;
}

void silenceErrors(const bool trueIfShouldSilence) noexcept
{
    g_silentErrors = trueIfShouldSilence;
}

// ========================================================
// Memory allocation callbacks:
// ========================================================

static void * defaultAllocCb(const std::size_t sizeInBytes, void * /* userContext */)
{
    CFG_ASSERT(sizeInBytes != 0);
    return std::malloc(sizeInBytes);
}

static void defaultDeallocCb(void * ptrToFree, void * /* userContext */)
{
    std::free(ptrToFree);
}

static MemoryAllocCallbacks g_memAlloc
{
    nullptr,
    &defaultAllocCb,
    &defaultDeallocCb
};

void setMemoryAllocCallbacks(MemoryAllocCallbacks * memCallbacks) noexcept
{
    if (memCallbacks == nullptr) // Use null to restore the defaults.
    {
        g_memAlloc.userContext = nullptr;
        g_memAlloc.alloc       = &defaultAllocCb;
        g_memAlloc.dealloc     = &defaultDeallocCb;
    }
    else
    {
        g_memAlloc = *memCallbacks;
    }
}

MemoryAllocCallbacks getMemoryAllocCallbacks() noexcept
{
    return g_memAlloc;
}

// ========================================================
// Internal memory allocator:
// ========================================================

template<typename T>
static T * memAlloc(const std::size_t countInItems)
{
    CFG_ASSERT(countInItems != 0);
    return static_cast<T *>(g_memAlloc.alloc(countInItems * sizeof(T), g_memAlloc.userContext));
}

template<typename T>
static void memFree(T * ptrToFree) noexcept
{
    if (ptrToFree != nullptr)
    {
        // The incoming pointer may be const, hence the C-style cast.
        g_memAlloc.dealloc((void *)ptrToFree, g_memAlloc.userContext);
    }
}

template<typename T>
static T * construct(T * obj, const T & other) // Copy constructor
{
    return ::new(obj) T(other);
}

template<typename T, typename... Args>
static T * construct(T * obj, Args&&... args) // Parameterized or default constructor
{
    return ::new(obj) T(std::forward<Args>(args)...);
}

template<typename T>
static void destroy(T * obj) noexcept
{
    if (obj != nullptr)
    {
        obj->~T();
    }
}

// ========================================================
// class DefaultFileIOCallbacksStdIn:
// ========================================================

class DefaultFileIOCallbacksStdIn final
    : public FileIOCallbacks
{
public:
    ~DefaultFileIOCallbacksStdIn();

    bool open(FileHandle * outHandle, const char * const filename, const FileOpenMode mode) override
    {
        if (outHandle == nullptr || filename == nullptr || *filename == '\0')
        {
            return false;
        }

        FILE * file;
        const char * const modeStr = (mode == FileOpenMode::Read ? "rt" : "wt");

        // fopen_s avoids a deprecation warning for std::fopen on MSVC.
        #ifdef _MSC_VER
        if (fopen_s(&file, filename, modeStr) != 0)
        {
            file = nullptr;
        }
        #else // !_MSC_VER
        file = std::fopen(filename, modeStr);
        #endif // _MSC_VER

        if (file == nullptr)
        {
            errorF("Unable to open file \"%s\" with mode '%s'.", filename, modeStr);
            (*outHandle) = nullptr;
            return false;
        }

        (*outHandle) = file;
        return true;
    }

    void close(FileHandle fh) override
    {
        if (fh == nullptr)
        {
            return;
        }
        std::fclose(static_cast<FILE *>(fh));
    }

    bool isAtEOF(FileHandle fh) const override
    {
        if (fh == nullptr)
        {
            return true;
        }
        return std::feof(static_cast<FILE *>(fh)) ? true : false;
    }

    void rewind(FileHandle fh) override
    {
        if (fh == nullptr)
        {
            return;
        }
        std::rewind(static_cast<FILE *>(fh));
    }

    bool readLine(FileHandle fh, char * outBuffer, const int bufferSize) override
    {
        if (fh == nullptr || outBuffer == nullptr || bufferSize <= 0)
        {
            return false;
        }
        return std::fgets(outBuffer, bufferSize, static_cast<FILE *>(fh)) != nullptr;
    }

    bool writeString(FileHandle fh, const char * const str) override
    {
        if (fh == nullptr || str == nullptr || *str == '\0')
        {
            return false;
        }
        return std::fputs(str, static_cast<FILE *>(fh)) != EOF;
    }

    bool writeFormat(FileHandle fh, const char * const fmt, ...) override CFG_PRINTF_FUNC(3, 4)
    {
        if (fh == nullptr || fmt == nullptr)
        {
            return false;
        }

        va_list vaList;
        char tempStr[2048];

        va_start(vaList, fmt);
        const int result = std::vsnprintf(tempStr, lengthOfArray(tempStr), fmt, vaList);
        va_end(vaList);

        if (result > 0)
        {
            return std::fputs(tempStr, static_cast<FILE *>(fh)) != EOF;
        }
        else
        {
            return false;
        }
    }
};

// ========================================================

DefaultFileIOCallbacksStdIn::~DefaultFileIOCallbacksStdIn()
{ }

FileIOCallbacks::~FileIOCallbacks()
{ }

// ========================================================
// File IO Callbacks:
// ========================================================

static DefaultFileIOCallbacksStdIn g_defaultFileIO{};
static FileIOCallbacks *           g_fileIO = &g_defaultFileIO;

void setFileIOCallbacks(FileIOCallbacks * fileIO) noexcept
{
    if (fileIO == nullptr) // Use null to restore the default.
    {
        g_fileIO = &g_defaultFileIO;
    }
    else
    {
        g_fileIO = fileIO;
    }
}

FileIOCallbacks * getFileIOCallbacks() noexcept
{
    return g_fileIO;
}

// ========================================================
// Boolean value strings for string CVars:
// ========================================================

static const BoolCStr g_defaultBoolStrings[]
{
    { "true"  , "false" },
    { "yes"   , "no"    },
    { "on"    , "off"   },
    { "1"     , "0"     },
    { nullptr , nullptr }
};

static const BoolCStr * g_currentBoolStrings = g_defaultBoolStrings;

void setBoolStrings(const BoolCStr * const strings) noexcept
{
    if (strings == nullptr) // Use null to restore the defaults.
    {
        g_currentBoolStrings = g_defaultBoolStrings;
    }
    else
    {
        g_currentBoolStrings = strings;
    }
}

const BoolCStr * getBoolStrings() noexcept
{
    return g_currentBoolStrings;
}

// ========================================================
// Strings utility functions:
// ========================================================

// Length in characters of ASCII string.
static inline int lengthOfString(const char * const str)
{
    CFG_ASSERT(str != nullptr);

    // Don't care about 32-64bit truncation.
    // Our strings are not nearly that long.
    return static_cast<int>(std::strlen(str));
}

// True for anything less that the ASCII space character (32)
static inline bool isWhitespace(const int c) noexcept
{
    return c <= ' ';
}

static int copyString(char * dest, int destSizeInChars, const char * source)
{
    CFG_ASSERT(dest   != nullptr);
    CFG_ASSERT(source != nullptr);
    CFG_ASSERT(destSizeInChars > 0);

    // Copy until the end of source or until we run out of space in dest:
    char * ptr = dest;
    while ((*ptr++ = *source++) && --destSizeInChars > 0) { }

    // Truncate on overflow:
    if (destSizeInChars == 0)
    {
        *(--ptr) = '\0';
        errorF("Overflow in cfg::copyString()! Output was truncated.");
        return static_cast<int>(ptr - dest);
    }

    // Return the number of chars written to dest (not counting the null terminator).
    return static_cast<int>(ptr - dest - 1);
}

static char * cloneString(const char * const src)
{
    CFG_ASSERT(src != nullptr);

    const int len = lengthOfString(src) + 1;
    char * newString = memAlloc<char>(len);
    copyString(newString, len, src);

    return newString;
}

static const char ** cloneStringArray(const char ** strings)
{
    if (strings == nullptr)
    {
        return nullptr;
    }

    int i;
    char * str;
    const char ** ptr;
    int totalLength = 0;

    for (i = 0; strings[i] != nullptr; ++i)
    {
        totalLength += lengthOfString(strings[i]) + 1;
    }

    // Allocate everything (pointers + strings) as a single memory chunk:
    const std::size_t pointerBytes = (i + 1) * sizeof(char *);
    ptr = reinterpret_cast<const char **>(memAlloc<std::uint8_t>(pointerBytes + totalLength));
    str = reinterpret_cast<char *>(reinterpret_cast<std::uint8_t *>(ptr) + pointerBytes);

    for (i = 0; strings[i] != nullptr; ++i)
    {
        ptr[i] = str;
        std::strcpy(str, strings[i]);
        str += lengthOfString(strings[i]) + 1;
    }

    // Array is null terminated.
    ptr[i] = nullptr;
    return ptr;
}

static int compareStringsNoCase(const char * s1, const char * s2, std::uint32_t count)
{
    CFG_ASSERT(s1 != nullptr);
    CFG_ASSERT(s2 != nullptr);

    if (s1 == s2)
    {
        return 0; // Same pointers, same string.
    }

    int c1, c2;
    do
    {
        if (count == 0)
        {
            return 0; // Strings are equal until end.
        }
        --count;

        c1 = *s1++;
        c2 = *s2++;

        int diff = c1 - c2;
        while (diff) // Not the same char? Try changing the case...
        {
            if (c1 <= 'Z' && c1 >= 'A')
            {
                diff += ('a' - 'A');
                if (!diff) { break; }
            }
            if (c2 <= 'Z' && c2 >= 'A')
            {
                diff -= ('a' - 'A');
                if (!diff) { break; }
            }
            return (diff < 0) ? -1 : (diff > 0) ? 1 : 0;
        }
    }
    while (c1);

    return 0; // Strings are equal.
}

static char * rightTrimString(char * strIn)
{
    CFG_ASSERT(strIn != nullptr);
    if (*strIn == '\0')
    {
        return strIn;
    }

    int length = lengthOfString(strIn);
    char * ptr = strIn + length - 1;

    while (length > 0 && isWhitespace(*ptr))
    {
        *ptr-- = '\0';
        length--;
    }

    return strIn;
}

static bool intToString(std::uint64_t number, char * dest, const int destSizeInChars, const int numBase, const bool isNegative)
{
    CFG_ASSERT(dest != nullptr);
    CFG_ASSERT(destSizeInChars > 3); // - or 0x and a '\0'

    // Supports binary, octal, decimal and hexadecimal.
    const bool goodBase = (numBase == 2  || numBase == 8 ||
                           numBase == 10 || numBase == 16);
    if (!goodBase)
    {
        dest[0] = '\0';
        return errorF("Bad numeric base (%i)!", numBase);
    }

    char * ptr = dest;
    int length = 0;

    if (numBase == 16)
    {
        // Add an "0x" in front of hexadecimal values:
        *ptr++ = '0';
        *ptr++ = 'x';
        length += 2;
    }
    else
    {
        if (isNegative && numBase == 10)
        {
            // Negative decimal, so output '-' and negate:
            length++;
            *ptr++ = '-';
            number = static_cast<std::uint64_t>(-static_cast<std::int64_t>(number));
        }
    }

    // Save pointer to the first digit so we can reverse the string later.
    char * firstDigit = ptr;

    // Main conversion loop:
    do
    {
        const int digitVal = number % numBase;
        number /= numBase;

        // Convert to ASCII and store:
        if (digitVal > 9)
        {
            *ptr++ = static_cast<char>((digitVal - 10) + 'A'); // A letter (hexadecimal)
        }
        else
        {
            *ptr++ = static_cast<char>(digitVal + '0'); // A digit
        }
        ++length;
    } while (number != 0 && length < destSizeInChars);

    // Check for buffer overflow. Return an empty string in such case.
    if (length >= destSizeInChars)
    {
        dest[0] = '\0';
        return errorF("Buffer overflow in integer => string conversion!");
    }

    // Terminate the string with a null char.
    *ptr-- = '\0';

    // We now have the digits of the number in the buffer,
    // but in reverse order. So reverse the string now.
    do
    {
        const char tmp = *ptr;
        *ptr = *firstDigit;
        *firstDigit = tmp;
        --ptr;
        ++firstDigit;
    } while (firstDigit < ptr);

    // Converted successfully.
    return true;
}

static int trimTrailingZeros(char * str)
{
    CFG_ASSERT(str != nullptr);

    int length = 0;
    for (; *str != '\0'; ++str, ++length)
    {
        if (*str == '.')
        {
            // Find the end of the string
            while (*++str != '\0')
            {
                ++length;
            }
            ++length;

            // Remove trailing zeros
            while (*--str == '0')
            {
                *str = '\0';
                --length;
            }

            // If the dot was left alone at the end, remove it.
            if (*str == '.')
            {
                *str = '\0';
                --length;
            }
            break;
        }
    }
    return length;
}

// ========================================================
// StringHasher/StringHasherNoCase Functors:
//
// Simple and fast One-at-a-Time (OAT) hash algorithm
// http://en.wikipedia.org/wiki/Jenkins_hash_function
// ========================================================

struct StringHasher final
{
    std::uint32_t operator()(const char * str) const
    {
        CFG_ASSERT(str != nullptr);

        std::uint32_t h = 0;
        while (*str != '\0')
        {
            h += *str++;
            h += (h << 10);
            h ^= (h >> 6);
        }

        h += (h << 3);
        h ^= (h >> 11);
        h += (h << 15);
        return h;
    }
};

struct StringHasherNoCase final
{
    std::uint32_t operator()(const char * str) const
    {
        CFG_ASSERT(str != nullptr);

        std::uint32_t h = 0;
        while (*str != '\0')
        {
            h += std::tolower(*str++);
            h += (h << 10);
            h ^= (h >> 6);
        }

        h += (h << 3);
        h ^= (h >> 11);
        h += (h << 15);
        return h;
    }
};

// ========================================================
// class HashTableLink:
//
// Commands and CVars are stored in a linked hash table
// to accelerate lookup by name. We use an intrusive data
// structure to avoid additional memory allocations, so
// CommandHandler and CVar classes must inherit from this
// node type to be able to insert themselves into the HT.
// ========================================================

#if CFG_PACK_STRUCTURES
    #pragma pack(push, 1)
#endif // CFG_PACK_STRUCTURES

template<typename T>
class HashTableLink
{
public:

    template<typename IT, typename HF>
    friend class LinkedHashTable;

    HashTableLink() = default;

    T * getNext() const noexcept
    {
        return listNext;
    }
    std::uint32_t getHashKey() const noexcept
    {
        return hashKey;
    }

protected:

    // Hash links are not meant to be directly deleted,
    // so we don't need a public virtual destructor.
    ~HashTableLink() = default;

    std::uint32_t hashKey = 0; // Hash value of the node's key. Never zero for a linked node.
    T * hashNext = nullptr;    // Link to next element in the hash table bucket chain. Null for the last node.
    T * listNext = nullptr;    // Link to next element is the singly-linked list of all nodes. Null for the last node.
};

#if CFG_PACK_STRUCTURES
    #pragma pack(pop)
#endif // CFG_PACK_STRUCTURES

// ========================================================
// class LinkedHashTable:
//
// The intrusive hash table template used to order
// Commands and CVars by name. Elements must inherit
// from HashTableLink to be inserted into this data
// structure. It doesn't own the nodes, so the table
// won't try to delete the objects upon destruction.
// The size of the table in buckets is fixed and never
// changes once constructed. We supply a reasonable
// default (DefaultHTSizeHint) that should be enough for
// most applications. The only issue of getting close
// to the hash table max size is that item collisions
// increase, but the structure should continue to work
// just fine. Key type is fixed to C-style char* strings.
// ========================================================

template<typename T, typename HF>
class LinkedHashTable final
{
public:

    // Default value: A prime number close to 1024.
    // If redefining this, make sure to keep it a prime
    // number to ensure a lower number of hash key collisions.
    // Larger values should improve lookup speed, but will use
    // more memory (each table entry is the size of a pointer).
    static constexpr int DefaultHTSizeHint = 1033;

    // User provided hash functor.
    // Takes a string and returns an unsigned integer.
    // See the above StringHasher/StringHasherNoCase.
    using HashFunc = HF;

    // Not copyable.
    LinkedHashTable(const LinkedHashTable &) = delete;
    LinkedHashTable & operator = (const LinkedHashTable &) = delete;

    LinkedHashTable() = default;
    ~LinkedHashTable() { deallocate(); }

    void allocate(const int sizeInBuckets = DefaultHTSizeHint)
    {
        if (table != nullptr)
        {
            return; // Already allocated.
        }

        table = memAlloc<T *>(sizeInBuckets);
        bucketCount = sizeInBuckets;
        clearArray(table, sizeInBuckets);
    }

    void deallocate() noexcept
    {
        memFree(table);
        table       = nullptr;
        listHead    = nullptr;
        bucketCount = 0;
        usedBuckets = 0;
    }

    T * findByKey(const char * const key) const
    {
        CFG_ASSERT(key != nullptr);
        if (isEmpty())
        {
            return nullptr;
        }

        const std::uint32_t hashKey = hashOf(key);
        for (T * node = table[hashKey % bucketCount]; node; node = node->hashNext)
        {
            if (hashKey == node->hashKey)
            {
                return node;
            }
        }
        return nullptr; // Not found.
    }

    void linkWithKey(T * node, const char * const key)
    {
        CFG_ASSERT(key  != nullptr);
        CFG_ASSERT(node != nullptr);
        CFG_ASSERT(node->hashKey == 0); // Can't be linked more than once!

        allocate(); // Ensure buckets are allocated if this is the first use.

        const std::uint32_t hashKey = hashOf(key);
        const std::uint32_t bucket  = hashKey % bucketCount;

        // Make the new head node of its chain:
        node->hashKey  = hashKey;
        node->hashNext = table[bucket];
        table[bucket]  = node;

        // Prepend to the sequential chain of all nodes:
        node->listNext = listHead;
        listHead = node;

        ++usedBuckets;
    }

    T * unlinkByKey(const char * const key)
    {
        CFG_ASSERT(key != nullptr);
        if (isEmpty())
        {
            return nullptr;
        }

        const std::uint32_t hashKey = hashOf(key);
        const std::uint32_t bucket  = hashKey % bucketCount;

        T * previous = nullptr;
        for (T * node = table[bucket]; node;)
        {
            if (hashKey == node->hashKey)
            {
                if (previous != nullptr)
                {
                    // Not the head of the chain, remove from middle:
                    previous->hashNext = node->hashNext;
                }
                else if (node == table[bucket] && node->hashNext == nullptr)
                {
                    // Single node bucket, clear the entry:
                    table[bucket] = nullptr;
                }
                else if (node == table[bucket] && node->hashNext != nullptr)
                {
                    // Head of chain with other node(s) following:
                    table[bucket] = node->hashNext;
                }
                else
                {
                    errorF("LinkedHashTable bucket chain is corrupted!");
                    return nullptr;
                }

                node->hashNext = nullptr;
                node->hashKey  = 0;
                unlinkFromList(node);
                --usedBuckets;
                return node;
            }

            previous = node;
            node = node->hashNext;
        }

        return nullptr; // No such node with the given key.
    }

    // Misc accessors:
    T *  getFirst() const noexcept { return listHead; }
    int  getSize()  const noexcept { return usedBuckets; }
    bool isEmpty()  const noexcept { return usedBuckets == 0; }

private:

    std::uint32_t hashOf(const char * const key) const
    {
        const std::uint32_t hashKey = HashFunc{}(key);
        CFG_ASSERT(hashKey != 0 && "Null hash indexes not allowed!");
        return hashKey;
    }

    void unlinkFromList(T * node)
    {
        if (node == listHead) // Easy case, remove the fist node.
        {
            listHead = listHead->listNext;
            node->listNext = nullptr;
            return;
        }

        T * curr = listHead;
        T * prev = nullptr;

        // Find the element before the node to remove:
        while (curr != node)
        {
            prev = curr;
            curr = curr->listNext;
        }

        // Should have found the node somewhere...
        CFG_ASSERT(curr != nullptr);
        CFG_ASSERT(prev != nullptr);

        // Unlink the node by pointing its previous element to what was after it:
        prev->listNext = node->listNext;
        node->listNext = nullptr;
    }

    // Array of pointers to items (the buckets).
    T ** table = nullptr;

    // First node in the linked list of all table nodes.
    // This will point to the most recently inserted item.
    T * listHead = nullptr;

    // Total size in buckets (items) of table[] and buckets used so far.
    int bucketCount = 0;
    int usedBuckets = 0;
};

// ================================================================================================
//
//                              CVars - Configuration Variables
//
// ================================================================================================

// ========================================================
// CVar helpers:
// ========================================================

template<typename T>
struct CVarNumberRange final
{
    T minValue = 0;
    T maxValue = 0;

    CVarNumberRange() = default;

    CVarNumberRange(const T min, const T max) noexcept
        : minValue(min)
        , maxValue(max)
    { }

    int getCount() const noexcept
    {
        return 2;
    }

    T getValue(const int index) const
    {
        CFG_ASSERT(index >= 0 && index < getCount());
        return (index == 0 ? minValue : maxValue);
    }

    void clear() noexcept
    {
        minValue = 0;
        maxValue = 0;
    }
};

struct CVarAllowedStrings final
{
    // List of strings terminated by a null entry.
    const char ** strings = nullptr;

    CVarAllowedStrings(const char ** allowedStrings)
    {
        if (allowedStrings != nullptr)
        {
            strings = cloneStringArray(allowedStrings);
        }
    }

    int getCount() const noexcept
    {
        int n = 0;
        if (strings != nullptr)
        {
            for (; strings[n] != nullptr; ++n) { }
        }
        return n;
    }

    std::string getValue(const int index) const
    {
        CFG_ASSERT(index >= 0 && index < getCount());
        return strings[index];
    }

    void clear() noexcept
    {
        memFree(strings);
        strings = nullptr;
    }
};

struct CVarEnumConstList final
{
    const char  ** names  = nullptr; // List of strings terminated by a null entry.
    std::int64_t * values = nullptr; // Corresponding values for each name.

    CVarEnumConstList(const std::int64_t * enumConstants, const char ** constNames)
    {
        if (enumConstants != nullptr && constNames != nullptr)
        {
            names = cloneStringArray(constNames);

            // Expect the same number of names and constant values.
            int n = 0;
            for (; constNames[n] != nullptr; ++n) { }

            values = memAlloc<std::int64_t>(n);
            std::memcpy(values, enumConstants, n * sizeof(std::int64_t));
        }
    }

    int getCount() const noexcept
    {
        int n = 0;
        if (names != nullptr)
        {
            for (; names[n] != nullptr; ++n) { }
        }
        return n;
    }

    std::string getValue(const int index) const
    {
        CFG_ASSERT(index >= 0 && index < getCount());
        return names[index];
    }

    void clear() noexcept
    {
        memFree(names);
        names = nullptr;

        memFree(values);
        values = nullptr;
    }
};

struct CVarEnumConst final
{
    const char * name;  // Pointer into the CVarEnumConstList for the CVar.
    std::int64_t value; // Integer value of the enum constant.
};

// Size of small stack-declared strings for things like number => string conversions.
constexpr int CVarTempStrMaxSize = 128;

// Format specifier to snprintf() float/double values to strings.
// 'g'  = decimal or scientific notation, whichever is shorter.
// '.8' = up-to eight decimal digits of precision.
#ifndef CFG_FLOAT_PRINT_FMT
    #define CFG_FLOAT_PRINT_FMT "%.8g"
#endif // CFG_FLOAT_PRINT_FMT

// ========================================================
// String comparators with configurable case-sensitivity:
// ========================================================

static inline int cvarCmpStrings(const char * const a, const char * const b)
{
    #if CFG_CVAR_CASE_SENSITIVE_STRINGS
    return std::strcmp(a, b);
    #else // !CFG_CVAR_CASE_SENSITIVE_STRINGS
    return compareStringsNoCase(a, b);
    #endif // CFG_CVAR_CASE_SENSITIVE_STRINGS
}

static inline int cvarCmpNames(const char * const a, const char * const b)
{
    #if CFG_CVAR_CASE_SENSITIVE_NAMES
    return std::strcmp(a, b);
    #else // !CFG_CVAR_CASE_SENSITIVE_NAMES
    return compareStringsNoCase(a, b);
    #endif // CFG_CVAR_CASE_SENSITIVE_NAMES
}

static inline bool cvarNameStartsWith(const char * const name, const char * const prefix)
{
    CFG_ASSERT(name   != nullptr);
    CFG_ASSERT(prefix != nullptr);

    const int nameLen   = lengthOfString(name);
    const int prefixLen = lengthOfString(prefix);

    if (nameLen < prefixLen)
    {
        return false;
    }
    else if (nameLen == 0 || prefixLen == 0)
    {
        return false;
    }

    #if CFG_CVAR_CASE_SENSITIVE_NAMES
    return std::strncmp(name, prefix, prefixLen) == 0;
    #else // !CFG_CVAR_CASE_SENSITIVE_NAMES
    return compareStringsNoCase(name, prefix, prefixLen) == 0;
    #endif // CFG_CVAR_CASE_SENSITIVE_NAMES
}

static inline void cvarSortMatches(CVar ** outMatches, const int count)
{
    std::sort(outMatches, outMatches + count,
              [](const CVar * const a, const CVar * const b)
              {
                  return cvarCmpNames(a->getNameCString(), b->getNameCString()) < 0;
              });
}

static inline void cvarSortMatches(const char ** outMatches, const int count)
{
    std::sort(outMatches, outMatches + count,
              [](const char * const a, const char * const b)
              {
                  return cvarCmpNames(a, b) < 0;
              });
}

// ========================================================
// cvarToX() functions:
// ========================================================

//
// int64_t:
//

static inline std::int64_t cvarToInt64(const std::int64_t val)
{
    return val;
}

static inline std::int64_t cvarToInt64(const bool val)
{
    return static_cast<std::int64_t>(val);
}

static inline std::int64_t cvarToInt64(const double val)
{
    return static_cast<std::int64_t>(val);
}

static inline std::int64_t cvarToInt64(const CVarEnumConst & enumConst)
{
    return enumConst.value;
}

static inline std::int64_t cvarToInt64(const std::string & str, bool * outOkFlag = nullptr)
{
    char * endPtr   = nullptr;
    const auto nPtr = str.c_str();
    const auto num  = std::strtoll(nPtr, &endPtr, 0);

    if (endPtr == nullptr || endPtr == nPtr)
    {
        errorF("No available conversion from \"%s\" to integer number.", str.c_str());
        if (outOkFlag != nullptr) { *outOkFlag = false; }
        return 0;
    }

    if (outOkFlag != nullptr) { *outOkFlag = true; }
    return num;
}

//
// double:
//

static inline double cvarToDouble(const std::int64_t val)
{
    return static_cast<double>(val);
}

static inline double cvarToDouble(const bool val)
{
    return static_cast<double>(val);
}

static inline double cvarToDouble(const double val)
{
    return val;
}

static inline double cvarToDouble(const CVarEnumConst & enumConst)
{
    return static_cast<double>(enumConst.value);
}

static inline double cvarToDouble(const std::string & str, bool * outOkFlag = nullptr)
{
    char * endPtr   = nullptr;
    const auto nPtr = str.c_str();
    const auto num  = std::strtod(nPtr, &endPtr);

    if (endPtr == nullptr || endPtr == nPtr)
    {
        errorF("No available conversion from \"%s\" to floating-point number.", str.c_str());
        if (outOkFlag != nullptr) { *outOkFlag = false; }
        return 0.0;
    }

    if (outOkFlag != nullptr) { *outOkFlag = true; }
    return num;
}

//
// std::string:
//

static inline void cvarToString(std::string * result, const std::int64_t val,
                                const CVar::NumberFormat numberFormat)
{
    int base = 0;
    switch (numberFormat)
    {
    case CVar::NumberFormat::Binary      : { base = 2;  break; }
    case CVar::NumberFormat::Octal       : { base = 8;  break; }
    case CVar::NumberFormat::Decimal     : { base = 10; break; }
    case CVar::NumberFormat::Hexadecimal : { base = 16; break; }
    } // switch (numberFormat)

    char numStr[CVarTempStrMaxSize];
    intToString(static_cast<std::uint64_t>(val), numStr, lengthOfArray(numStr), base, (val < 0));
    *result = numStr;
}

static inline void cvarToString(std::string * result, const bool val, CVar::NumberFormat)
{
    // For printing, the first boolean string is always the one used.
    const BoolCStr * const bStrings = getBoolStrings();
    *result = (val ? bStrings[0].trueStr : bStrings[0].falseStr);
}

static inline void cvarToString(std::string * result, const double val, CVar::NumberFormat)
{
    char numStr[CVarTempStrMaxSize];
    std::snprintf(numStr, lengthOfArray(numStr), CFG_FLOAT_PRINT_FMT, val);

    const int length = trimTrailingZeros(numStr);
    result->assign(numStr, length);
}

static inline void cvarToString(std::string * result, const CVarEnumConst & enumConst,
                                const CVar::NumberFormat numberFormat)
{
    if (enumConst.name != nullptr && enumConst.name[0] != '\0')
    {
        *result = enumConst.name;
    }
    else // Reuse the int=>string conversion:
    {
        cvarToString(result, enumConst.value, numberFormat);
    }
}

static inline void cvarToString(std::string * result, std::string str, CVar::NumberFormat)
{
    *result = std::move(str);
}

// ========================================================
// cvarSetX() functions:
// ========================================================

//
// int64_t:
//

static inline bool cvarSetInt64(std::int64_t * outVal, const std::int64_t newVal,
                                const CVarNumberRange<std::int64_t> * valueRange, CVar::NumberFormat)
{
    if (valueRange != nullptr)
    {
        if (newVal < valueRange->minValue)
        {
            return errorF("Value %lli below minimum (%lli).", newVal, valueRange->minValue);
        }
        else if (newVal > valueRange->maxValue)
        {
            return errorF("Value %lli above maximum (%lli).", newVal, valueRange->maxValue);
        }
    }
    *outVal = newVal;
    return true;
}

static inline bool cvarSetInt64(bool * outVal, const std::int64_t newVal,
                                const CVarNumberRange<bool> *, CVar::NumberFormat)
{
    // Range never checked for booleans.
    // Anything above zero is true, else false.
    *outVal = (newVal > 0);
    return true;
}

static inline bool cvarSetInt64(double * outVal, const std::int64_t newVal,
                                const CVarNumberRange<double> * valueRange, CVar::NumberFormat)
{
    if (valueRange != nullptr)
    {
        if (newVal < valueRange->minValue)
        {
            return errorF("Value %lli below minimum (%f).", newVal, valueRange->minValue);
        }
        else if (newVal > valueRange->maxValue)
        {
            return errorF("Value %lli above maximum (%f).", newVal, valueRange->maxValue);
        }
    }
    *outVal = static_cast<double>(newVal);
    return true;
}

static inline bool cvarSetInt64(CVarEnumConst * outVal, const std::int64_t newVal,
                                const CVarEnumConstList * enumConstants, CVar::NumberFormat)
{
    if (enumConstants != nullptr && enumConstants->names != nullptr)
    {
        for (int c = 0; enumConstants->names[c] != nullptr; ++c)
        {
            if (enumConstants->values[c] == newVal)
            {
                outVal->name  = enumConstants->names[c];
                outVal->value = enumConstants->values[c];
                return true;
            }
        }
        return false; // Value not a member of this enum.
    }
    else
    {
        outVal->name  = ""; // Can't figure out the constant name without a const list.
        outVal->value = newVal;
        return true;
    }
}

static inline bool cvarSetInt64(std::string * outVal, const std::int64_t newVal,
                                const CVarAllowedStrings * allowedStrings,
                                const CVar::NumberFormat numberFormat)
{
    if (allowedStrings != nullptr && allowedStrings->strings != nullptr)
    {
        std::string tempStr;
        cvarToString(&tempStr, newVal, numberFormat);

        for (int s = 0; allowedStrings->strings[s] != nullptr; ++s)
        {
            if (cvarCmpStrings(allowedStrings->strings[s], tempStr.c_str()) == 0)
            {
                *outVal = std::move(tempStr);
                return true;
            }
        }
        return false;
    }
    else
    {
        cvarToString(outVal, newVal, numberFormat);
        return true;
    }
}

//
// double:
//

static inline bool cvarSetDouble(std::int64_t * outVal, const double newVal,
                                 const CVarNumberRange<std::int64_t> * valueRange)
{
    if (valueRange != nullptr)
    {
        if (newVal < valueRange->minValue)
        {
            return errorF("Value %f below minimum (%lli).", newVal, valueRange->minValue);
        }
        else if (newVal > valueRange->maxValue)
        {
            return errorF("Value %f above maximum (%lli).", newVal, valueRange->maxValue);
        }
    }
    *outVal = static_cast<std::int64_t>(newVal);
    return true;
}

static inline bool cvarSetDouble(bool * outVal, const double newVal,
                                 const CVarNumberRange<bool> *)
{
    *outVal = (newVal > 0.0);
    return true;
}

static inline bool cvarSetDouble(double * outVal, const double newVal,
                                 const CVarNumberRange<double> * valueRange)
{
    if (valueRange != nullptr)
    {
        if (newVal < valueRange->minValue)
        {
            return errorF("Value %f below minimum (%f).", newVal, valueRange->minValue);
        }
        else if (newVal > valueRange->maxValue)
        {
            return errorF("Value %f above maximum (%f).", newVal, valueRange->maxValue);
        }
    }
    *outVal = newVal;
    return true;
}

static inline bool cvarSetDouble(CVarEnumConst * outVal, const double newVal,
                                 const CVarEnumConstList * enumConstants)
{
    // Enums can only be integers, so we can reuse this other function.
    return cvarSetInt64(outVal, static_cast<std::int64_t>(newVal),
                        enumConstants, CVar::NumberFormat::Decimal);
}

static inline bool cvarSetDouble(std::string * outVal, const double newVal,
                                 const CVarAllowedStrings * allowedStrings)
{
    if (allowedStrings != nullptr && allowedStrings->strings != nullptr)
    {
        std::string tempStr;
        cvarToString(&tempStr, newVal, CVar::NumberFormat::Decimal);

        for (int s = 0; allowedStrings->strings[s] != nullptr; ++s)
        {
            if (cvarCmpStrings(allowedStrings->strings[s], tempStr.c_str()) == 0)
            {
                *outVal = std::move(tempStr);
                return true;
            }
        }
        return false;
    }
    else
    {
        cvarToString(outVal, newVal, CVar::NumberFormat::Decimal);
        return true;
    }
}

//
// std::string:
//

static inline bool cvarSetString(std::int64_t * outVal, std::string newVal,
                                 const CVarNumberRange<std::int64_t> * valueRange)
{
    bool ok;
    const auto temp = cvarToInt64(newVal, &ok);
    if (!ok)
    {
        return false;
    }
    return cvarSetInt64(outVal, temp, valueRange, CVar::NumberFormat::Decimal);
}

static inline bool cvarSetString(bool * outVal, std::string newVal,
                                 const CVarNumberRange<bool> *)
{
    const BoolCStr * const bStrings = getBoolStrings();
    bool bFound = false;

    for (int s = 0; bStrings[s].trueStr != nullptr; ++s)
    {
        if (cvarCmpStrings(bStrings[s].trueStr, newVal.c_str()) == 0)
        {
            bFound  = true;
            *outVal = true;
            break;
        }
        if (cvarCmpStrings(bStrings[s].falseStr, newVal.c_str()) == 0)
        {
            bFound  = true;
            *outVal = false;
            break;
        }
    }

    return (bFound ? true : errorF("Can't set boolean CVar from string \"%s\".", newVal.c_str()));
}

static inline bool cvarSetString(double * outVal, std::string newVal,
                                 const CVarNumberRange<double> * valueRange)
{
    bool ok;
    const auto temp = cvarToDouble(newVal, &ok);
    if (!ok)
    {
        return false;
    }
    return cvarSetDouble(outVal, temp, valueRange);
}

static inline bool cvarSetString(CVarEnumConst * outVal, std::string newVal,
                                 const CVarEnumConstList * enumConstants)
{
    if (enumConstants != nullptr && enumConstants->names != nullptr)
    {
        for (int c = 0; enumConstants->names[c] != nullptr; ++c)
        {
            if (enumConstants->names[c] == newVal)
            {
                outVal->name  = enumConstants->names[c];
                outVal->value = enumConstants->values[c];
                return true;
            }
        }
        return false; // Value not a member of this enum.
    }
    else
    {
        // Assume the value is numeric:
        std::int64_t temp;
        if (cvarSetString(&temp, std::move(newVal), nullptr))
        {
            outVal->name  = "";
            outVal->value = temp;
            return true;
        }
        return false;
    }
}

static inline bool cvarSetString(std::string * outVal, std::string newVal,
                                 const CVarAllowedStrings * allowedStrings)
{
    if (allowedStrings != nullptr && allowedStrings->strings != nullptr)
    {
        for (int s = 0; allowedStrings->strings[s] != nullptr; ++s)
        {
            if (cvarCmpStrings(allowedStrings->strings[s], newVal.c_str()) == 0)
            {
                *outVal = std::move(newVal);
                return true;
            }
        }
        return false;
    }
    else
    {
        *outVal = std::move(newVal);
        return true;
    }
}

// ========================================================
// class CVarImplBase:
// ========================================================

class CVarImplBase
    : public HashTableLink<CVarImplBase>
    , public CVar
{
public:
    // This empty class serves as glue for the HashTableLink
    // and CVar, so that CVarManagerImpl can declare a
    // hash table of CVarImplBases. This is necessary
    // because CVarImpl is a template, and we want to
    // store generic pointers in the manger instead.
    virtual ~CVarImplBase();

    // Sets the string value regardless of ReadOnly or InitOnly if the proper
    // flags are passed. This is used internally to set CVars from config files
    // and startup command line. It will also prevent setting the modified flag
    // again after setting the value.
    virtual bool setStringValueIgnoreRO(std::string newValue, bool writeRomCVars, bool writeInitCVars) = 0;
    virtual bool setDefaultValueIgnoreRO(bool writeRomCVars, bool writeInitCVars) = 0;

    // Formats a 'set' command for config file writing.
    char * toCfgString(char * outBuffer, int bufferSize) const;
};

char * CVarImplBase::toCfgString(char * outBuffer, const int bufferSize) const
{
    CFG_ASSERT(outBuffer != nullptr);

    std::string varFlagsString;
    const std::uint32_t flags = getFlags();

    // Only the ones that will be created dynamically need the flags.
    // The ones defined in the C++ code already have their flags set.
    if (flags & Flags::UserDefined)
    {
        if (flags & Flags::Persistent) { varFlagsString += "-persistent "; }
        if (flags & Flags::Volatile)   { varFlagsString += "-volatile ";   }
        if (flags & Flags::ReadOnly)   { varFlagsString += "-readonly ";   }
        if (flags & Flags::InitOnly)   { varFlagsString += "-initonly ";   }
        if (flags & Flags::Modified)   { varFlagsString += "-modified ";   }
    }

    const CVar::Type  varType        = getType();
    const std::string varValueString = getStringValue();

    // set  cvar  value  optional-flags
    if (!varFlagsString.empty())
    {
        if (varType == CVar::Type::String || varType == CVar::Type::Enum)
        {
            std::snprintf(outBuffer, bufferSize,
                          "set %s \"%s\" %s",
                          getNameCString(), varValueString.c_str(), varFlagsString.c_str());
        }
        else // No need to quote the value string.
        {
            std::snprintf(outBuffer, bufferSize,
                          "set %s %s %s",
                          getNameCString(), varValueString.c_str(), varFlagsString.c_str());
        }
    }
    else
    {
        if (varType == CVar::Type::String || varType == CVar::Type::Enum)
        {
            std::snprintf(outBuffer, bufferSize,
                          "set %s \"%s\"",
                          getNameCString(), varValueString.c_str());
        }
        else // No need to quote the value string.
        {
            std::snprintf(outBuffer, bufferSize,
                          "set %s %s",
                          getNameCString(), varValueString.c_str());
        }
    }
    return outBuffer;
}

// ========================================================
// class CVarImpl:
// ========================================================

template
<
    typename ValueType,
    typename ValueRange,
    CVar::Type TypeTag
>
class CVarImpl final
    : public CVarImplBase
{
public:

    // Not copyable.
    CVarImpl(const CVarImpl &) = delete;
    CVarImpl & operator = (const CVarImpl &) = delete;

    CVarImpl(const char * const varName, const char * const varDesc,
             const std::uint32_t varFlags, const ValueType & initValue,
             const ValueRange & range, const CVarValueCompletionCallback completionCb)
        : currentValue(initValue)
        , defaultValue(initValue)
        , valueRange(range)
        , numberFormat(NumberFormat::Decimal)
        , flags(varFlags)
        , name(nullptr)
        , description(nullptr)
        , valueCompletionCallback(completionCb)
    {
        // Must have a name string,
        CFG_ASSERT(varName != nullptr && *varName != '\0');
        name = cloneString(varName);

        // Description comment is optional.
        if (varDesc != nullptr && *varDesc != '\0')
        {
            description = cloneString(varDesc);
        }

        // This would make no sense.
        if ((varFlags & CVar::Flags::Persistent) && (varFlags & CVar::Flags::Volatile))
        {
            errorF("%s: 'Persistent' and 'Volatile' flags are mutually exclusive!", varName);
        }
    }

    ~CVarImpl()
    {
        memFree(name);
        memFree(description);
        valueRange.clear();
    }

    std::string getName() const override
    {
        return getNameCString();
    }

    std::string getDesc() const override
    {
        return getDescCString();
    }

    const char * getNameCString() const override
    {
        // Name is never null.
        CFG_ASSERT(name != nullptr);
        return name;
    }

    const char * getDescCString() const override
    {
        // Description can be null, returning an empty string for that case.
        return (description != nullptr ? description : "");
    }

    std::string getTypeString() const override
    {
        return getTypeCString();
    }

    const char * getTypeCString() const override
    {
        static const char * const typeStrings[]
        {
            "int",
            "bool",
            "float",
            "string",
            "enum"
        };

        const auto index = static_cast<int>(TypeTag);
        CFG_ASSERT(index < lengthOfArray(typeStrings));
        return typeStrings[index];
    }

    Type getType() const override
    {
        return TypeTag;
    }

    NumberFormat getNumberFormat() const override
    {
        return numberFormat;
    }

    void setNumberFormat(const NumberFormat format) override
    {
        numberFormat = format;
    }

    std::uint32_t getFlags() const override
    {
        return flags;
    }

    void setFlags(const std::uint32_t newFlags) override
    {
        flags = newFlags;
    }

    std::string getFlagsString() const override
    {
        std::string str;

        if (flags & Flags::Modified)    { str += "M "; }
        if (flags & Flags::Persistent)  { str += "P "; }
        if (flags & Flags::Volatile)    { str += "V "; }
        if (flags & Flags::ReadOnly)    { str += "R "; }
        if (flags & Flags::InitOnly)    { str += "I "; }
        if (flags & Flags::RangeCheck)  { str += "C "; }
        if (flags & Flags::UserDefined) { str += "U "; }

        if (str.empty())
        {
            str = "0";
        }
        else if (str.back() == ' ')
        {
            str.pop_back();
        }

        return str;
    }

    void setModified() override
    {
        flags |= Flags::Modified;
    }

    void clearModified() override
    {
        flags &= ~Flags::Modified;
    }

    bool isModified() const override
    {
        return flags & Flags::Modified;
    }

    bool isWritable() const override
    {
        return !(flags & (Flags::ReadOnly | Flags::InitOnly));
    }

    bool isPersistent() const override
    {
        return flags & Flags::Persistent;
    }

    bool isRangeChecked() const override
    {
        return flags & Flags::RangeCheck;
    }

    int compareNames(const CVar & other) const override
    {
        return cvarCmpNames(getNameCString(), other.getNameCString());
    }

    bool compareEqual(const CVar & other) const override
    {
        if (getType() != other.getType())
        {
            return false;
        }
        else if (getFlags() != other.getFlags())
        {
            return false;
        }
        else if (compareNames(other) != 0)
        {
            return false;
        }
        else if (getStringValue() != other.getStringValue())
        {
            return false;
        }
        else if (getDefaultValueString() != other.getDefaultValueString())
        {
            return false;
        }
        else if (getNumberFormat() != other.getNumberFormat())
        {
            return false;
        }
        else
        {
            // Description not compared. It's just metadata for displaying.
            // ValueRange also not currently compared. Should it be?
            return true;
        }
    }

    std::int64_t getIntValue() const override
    {
        return cvarToInt64(currentValue);
    }

    bool setIntValue(const std::int64_t newValue) override
    {
        if (!isWritable())
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        if (cvarSetInt64(&currentValue, newValue, (isRangeChecked() ? &valueRange : nullptr), numberFormat))
        {
            setModified();
            return true;
        }
        return false;
    }

    bool getBoolValue() const override
    {
        return !!cvarToInt64(currentValue);
    }

    bool setBoolValue(const bool newValue) override
    {
        if (!isWritable())
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        if (cvarSetInt64(&currentValue, newValue, (isRangeChecked() ? &valueRange : nullptr), numberFormat))
        {
            setModified();
            return true;
        }
        return false;
    }

    double getFloatValue() const override
    {
        return cvarToDouble(currentValue);
    }

    bool setFloatValue(const double newValue) override
    {
        if (!isWritable())
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        if (cvarSetDouble(&currentValue, newValue, (isRangeChecked() ? &valueRange : nullptr)))
        {
            setModified();
            return true;
        }
        return false;
    }

    std::string getStringValue() const override
    {
        std::string result;
        cvarToString(&result, currentValue, numberFormat);
        return result;
    }

    bool setStringValue(std::string newValue) override
    {
        if (!isWritable())
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        if (cvarSetString(&currentValue, std::move(newValue), (isRangeChecked() ? &valueRange : nullptr)))
        {
            setModified();
            return true;
        }
        return false;
    }

    bool setStringValueIgnoreRO(std::string newValue, const bool writeRomCVars, const bool writeInitCVars) override
    {
        // Optionally unchecked and without setting the modified flag.
        if ((flags & CVar::Flags::ReadOnly) && !writeRomCVars)
        {
            return errorF("CVar '%s' is read-only!", name);
        }
        if ((flags & CVar::Flags::InitOnly) && !writeInitCVars)
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        if (cvarSetString(&currentValue, std::move(newValue), (isRangeChecked() ? &valueRange : nullptr)))
        {
            return true;
        }
        return false;
    }

    int getAllowedValueStrings(std::string * outValueStrings, const int maxValueStrings) const override
    {
        if (outValueStrings == nullptr || maxValueStrings <= 0)
        {
            return -1;
        }

        std::string tempStr;
        const int count = std::min(maxValueStrings, valueRange.getCount());
        for (int v = 0; v < count; ++v)
        {
            cvarToString(&tempStr, valueRange.getValue(v), numberFormat);
            outValueStrings[v] = std::move(tempStr);
        }

        return valueRange.getCount();
    }

    int getAllowedValueCount() const override
    {
        return valueRange.getCount();
    }

    bool setDefaultValue() override
    {
        if (!isWritable())
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        currentValue = defaultValue;
        setModified();
        return true;
    }

    bool setDefaultValueIgnoreRO(const bool writeRomCVars, const bool writeInitCVars) override
    {
        // Optionally unchecked and without setting the modified flag.
        if ((flags & CVar::Flags::ReadOnly) && !writeRomCVars)
        {
            return errorF("CVar '%s' is read-only!", name);
        }
        if ((flags & CVar::Flags::InitOnly) && !writeInitCVars)
        {
            return errorF("CVar '%s' is read-only!", name);
        }

        currentValue = defaultValue;
        return true;
    }

    std::string getDefaultValueString() const override
    {
        std::string result;
        cvarToString(&result, defaultValue, numberFormat);
        return result;
    }

    int valueCompletion(const char * const partialVal, std::string * outMatches, const int maxMatches) const override
    {
        if (valueCompletionCallback != nullptr)
        {
            return valueCompletionCallback(partialVal, outMatches, maxMatches);
        }
        else
        {
            return getAllowedValueStrings(outMatches, maxMatches);
        }
    }

    CVarValueCompletionCallback getValueCompletionCallback() const override
    {
        return valueCompletionCallback;
    }

private:

    ValueType                   currentValue;            // Current value. Only changeable if not ReadOnly.
    const ValueType             defaultValue;            // The initial value set when created is the reset value.
    ValueRange                  valueRange;              // Range of numeric values or allowed strings/enum names.
    NumberFormat                numberFormat;            // Number formatting from int=>string conversion (decimal, binary, etc).
    std::uint32_t               flags;                   // ORed CVar::Flags or zero.
    const char *                name;                    // Heap-allocated name string. Never null and never empty.
    const char *                description;             // Heap-allocated Description comment. Can be null if not provided.
    CVarValueCompletionCallback valueCompletionCallback; // Optional callback for value auto-completion. May be null.
};

//
// These are the supported CVar types:
//
using CVarInt    = CVarImpl< std::int64_t,  CVarNumberRange<std::int64_t>, CVar::Type::Int    >;
using CVarBool   = CVarImpl< bool,          CVarNumberRange<bool>,         CVar::Type::Bool   >;
using CVarFloat  = CVarImpl< double,        CVarNumberRange<double>,       CVar::Type::Float  >;
using CVarString = CVarImpl< std::string,   CVarAllowedStrings,            CVar::Type::String >;
using CVarEnum   = CVarImpl< CVarEnumConst, CVarEnumConstList,             CVar::Type::Enum   >;

// ========================================================
// class CVarManagerImpl:
// ========================================================

class CVarManagerImpl final
    : public CVarManager
{
public:

    // Not copyable.
    CVarManagerImpl(const CVarManagerImpl &) = delete;
    CVarManagerImpl & operator = (const CVarManagerImpl &) = delete;

    explicit CVarManagerImpl(int hashTableSize);
    ~CVarManagerImpl();

    CVar * findCVar(const char * name) const override;
    int findCVarsWithPartialName(const char * partialName, CVar ** outMatches, int maxMatches) const override;
    int findCVarsWithPartialName(const char * partialName, const char ** outMatches, int maxMatches) const override;
    int findCVarsWithFlags(std::uint32_t flags, CVar ** outMatches, int maxMatches) const override;

    bool removeCVar(const char * name) override;
    bool removeCVar(CVar * cvar) override;
    void removeAllCVars() override;

    int getRegisteredCVarsCount() const override;
    bool isValidCVarName(const char * name) const override;
    void enumerateAllCVars(CVarEnumerateCallback enumCallback, void * userContext) override;

    CVar * registerCVarBool(const char * name, const char * description, std::uint32_t flags,
                            bool initValue, CVarValueCompletionCallback completionCb = nullptr) override;

    CVar * registerCVarInt(const char * name, const char * description, std::uint32_t flags,
                           std::int64_t initValue, std::int64_t minValue, std::int64_t maxValue,
                           CVarValueCompletionCallback completionCb = nullptr) override;

    CVar * registerCVarFloat(const char * name, const char * description, std::uint32_t flags,
                             double initValue, double minValue, double maxValue,
                             CVarValueCompletionCallback completionCb = nullptr) override;

    CVar * registerCVarString(const char * name, const char * description, std::uint32_t flags,
                              const std::string & initValue, const char ** allowedStrings,
                              CVarValueCompletionCallback completionCb = nullptr) override;

    CVar * registerCVarEnum(const char * name, const char * description, std::uint32_t flags,
                            std::int64_t initValue, const std::int64_t * enumConstants, const char ** constNames,
                            CVarValueCompletionCallback completionCb = nullptr) override;

    bool getCVarValueBool(const char * name) const override;
    std::int64_t getCVarValueInt(const char * name) const override;
    double getCVarValueFloat(const char * name) const override;
    std::string getCVarValueString(const char * name) const override;

    CVar * setCVarValueBool(const char * name, bool value, std::uint32_t flags) override;
    CVar * setCVarValueInt(const char * name, std::int64_t value, std::uint32_t flags) override;
    CVar * setCVarValueFloat(const char * name, double value, std::uint32_t flags) override;
    CVar * setCVarValueString(const char * name, const std::string & value, std::uint32_t flags) override;

    // For use in 'set' and 'reset' commands called on 'reloadConfig' or from the program command line.
    bool internalSetStringValue(CVar * cvar, std::string value);
    bool internalSetDefaultValue(CVar * cvar);

    // This will only affect 'set' and 'reset' commands on vars flagged with ReadOnly or InitOnly.
    void setAllowWritingReadOnlyVars(bool allow) noexcept;

    // This will only affect 'set' and 'reset' commands on vars flagged with InitOnly.
    void setAllowWritingInitOnlyVars(bool allow) noexcept;

private:

    template<typename T>
    bool validateCVarRegistration(const char * name, std::uint32_t flags,
                                  const T & initValue, T (CVar::*pGetVal)() const) const;

    template<typename T>
    int findWithPartialNameHelper(const char * partialName, T * outMatches,
                                  int maxMatches, T (*pGetVar)(CVar *)) const;

    //
    // Hash function used to lookup CVar names in the HT.
    // Can be case-sensitive or not.
    //
    #if CFG_CVAR_CASE_SENSITIVE_NAMES
    using CVarNameHasher = StringHasher;
    #else // !CFG_CVAR_CASE_SENSITIVE_NAMES
    using CVarNameHasher = StringHasherNoCase;
    #endif // CFG_CVAR_CASE_SENSITIVE_NAMES

    // All the registered CVars in a hash table for fast lookup by name.
    LinkedHashTable<CVarImplBase, CVarNameHasher> registeredCVars;
    bool allowWritingRomCVars;
    bool allowWritingInitCVars;
};

// ========================================================
// CVarManagerImpl implementation:
// ========================================================

CVarManager * CVarManager::createInstance(const int cvarHashTableSizeHint)
{
    auto cvarManager = memAlloc<CVarManagerImpl>(1);
    return construct(cvarManager, cvarHashTableSizeHint);
}

void CVarManager::destroyInstance(CVarManager * cvarManager)
{
    destroy(cvarManager);
    memFree(cvarManager);
}

CVarManagerImpl::CVarManagerImpl(const int hashTableSize)
    : allowWritingRomCVars(false)
    , allowWritingInitCVars(false)
{
    if (hashTableSize > 0)
    {
        registeredCVars.allocate(hashTableSize);
    }
}

CVarManagerImpl::~CVarManagerImpl()
{
    auto cvar = registeredCVars.getFirst();
    while (cvar != nullptr)
    {
        auto temp = cvar->getNext();
        destroy(cvar);
        memFree(cvar);
        cvar = temp;
    }
}

CVar * CVarManagerImpl::findCVar(const char * const name) const
{
    if (name == nullptr || *name == '\0')
    {
        return nullptr;
    }
    return registeredCVars.findByKey(name);
}

int CVarManagerImpl::findCVarsWithPartialName(const char * const partialName,
                                              CVar ** outMatches, const int maxMatches) const
{
    return findWithPartialNameHelper<CVar *>(partialName, outMatches, maxMatches,
                                             [](CVar * cvar) { return cvar; });
}

int CVarManagerImpl::findCVarsWithPartialName(const char * const partialName,
                                              const char ** outMatches, const int maxMatches) const
{
    return findWithPartialNameHelper<const char *>(partialName, outMatches, maxMatches,
                                                   [](CVar * cvar) { return cvar->getNameCString(); });
}

int CVarManagerImpl::findCVarsWithFlags(const std::uint32_t flags,
                                        CVar ** outMatches, const int maxMatches) const
{
    if (flags == 0)
    {
        return 0;
    }
    else if (outMatches == nullptr || maxMatches <= 0)
    {
        return -1;
    }

    int matchesFound = 0;
    for (auto cvar = registeredCVars.getFirst(); cvar; cvar = cvar->getNext())
    {
        if (cvar->getFlags() & flags)
        {
            if (matchesFound < maxMatches)
            {
                outMatches[matchesFound] = cvar;
            }
            ++matchesFound; // Keep incrementing even if outMatches[] is full,
                            // so the caller can know the total num found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        cvarSortMatches(outMatches, std::min(matchesFound, maxMatches));
    }
    return matchesFound;
}

template<typename T>
int CVarManagerImpl::findWithPartialNameHelper(const char * const partialName, T * outMatches,
                                               const int maxMatches, T (*pGetVar)(CVar *)) const
{
    if (partialName == nullptr || *partialName == '\0')
    {
        return 0;
    }
    else if (outMatches == nullptr || maxMatches <= 0)
    {
        return -1;
    }

    int matchesFound = 0;

    // Partial name searching unfortunately can't take advantage of the optimized
    // constant time hash table lookup, so we have to fall back to a linear search.
    for (auto cvar = registeredCVars.getFirst(); cvar; cvar = cvar->getNext())
    {
        if (cvarNameStartsWith(cvar->getNameCString(), partialName))
        {
            if (matchesFound < maxMatches)
            {
                outMatches[matchesFound] = pGetVar(cvar);
            }
            ++matchesFound; // Keep incrementing even if outMatches[] is full,
                            // so the caller can know the total num found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        cvarSortMatches(outMatches, std::min(matchesFound, maxMatches));
    }
    return matchesFound;
}

bool CVarManagerImpl::removeCVar(const char * const name)
{
    if (!isValidCVarName(name))
    {
        return errorF("'%s' is not a valid CVar name. Nothing to remove.", name);
    }

    auto cvar = registeredCVars.unlinkByKey(name);
    if (cvar == nullptr)
    {
        return false; // No such variable.
    }

    destroy(cvar);
    memFree(cvar);
    return true;
}

bool CVarManagerImpl::removeCVar(CVar * cvar)
{
    if (cvar == nullptr)
    {
        return false;
    }
    return removeCVar(cvar->getNameCString());
}

void CVarManagerImpl::removeAllCVars()
{
    auto cvar = registeredCVars.getFirst();
    while (cvar != nullptr)
    {
        auto temp = cvar->getNext();
        destroy(cvar);
        memFree(cvar);
        cvar = temp;
    }
    registeredCVars.deallocate();
}

int CVarManagerImpl::getRegisteredCVarsCount() const
{
    return registeredCVars.getSize();
}

bool CVarManagerImpl::isValidCVarName(const char * const name) const
{
    //
    // CVar names somewhat follow the C++ variable
    // naming rules, that is, cannot start with a number,
    // cannot contain white spaces, cannot start with nor
    // contain any special characters except the underscore.
    //
    // But we allow a CVar name composed by multiple words
    // separated by a dot, such as "Obj.Prop", as long as the
    // character following the dot complies to the above rules.
    //
    if (name == nullptr || *name == '\0')
    {
        return false;
    }

    int i = 0;
    int c = name[i++];

    // Must start with a letter or an underscore:
    if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c != '_'))
    {
        return false;
    }

    while (name[i] != '\0')
    {
        c = name[i++];

        // If not a number nor a lower or upper case letter...
        if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z'))
        {
            // If it is a dot or an underscore it's OK, otherwise invalid.
            if (c != '.' && c != '_')
            {
                return false;
            }

            // As long as the next char is a letter or underscore
            // and the string is not over after the dot...
            if (c == '.' && name[i] == '\0')
            {
                return false;
            }

            // Dot must be followed by letter or underscore.
            if (c == '.')
            {
                c = name[i++];
                if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && c != '_')
                {
                    return false;
                }
            }
            else // An '_'
            {
                // Underscore can be followed by letter,
                // number, another underscore or a dot.
                c = name[i++];
                if ((c < '0' || c > '9') &&
                    (c < 'a' || c > 'z') &&
                    (c < 'A' || c > 'Z') &&
                    c != '_' && c != '.')
                {
                    return false;
                }
            }
        }
    }

    // A valid name if we get to the end of the string.
    return true;
}

void CVarManagerImpl::enumerateAllCVars(CVarEnumerateCallback enumCallback, void * userContext)
{
    CFG_ASSERT(enumCallback != nullptr);
    for (auto cvar = registeredCVars.getFirst(); cvar; cvar = cvar->getNext())
    {
        if (!enumCallback(cvar, userContext))
        {
            return;
        }
    }
}

template<typename T>
bool CVarManagerImpl::validateCVarRegistration(const char * const name, const std::uint32_t flags,
                                               const T & initValue, T (CVar::*pGetVal)() const) const
{
    if (name == nullptr || *name == '\0')
    {
        return errorF("Null or empty string for CVar name!");
    }
    else if (!isValidCVarName(name))
    {
        return errorF("Invalid CVar name '%s'. Can't register it.", name);
    }
    else if (const auto cvar = findCVar(name))
    {
        if (cvar->getFlags() != flags)
        {
            errorF("CVar '%s' already registered with different flags!", name);
        }
        else if ((cvar->*pGetVal)() != initValue)
        {
            errorF("CVar '%s' already registered with different value!", name);
        }
        else
        {
            errorF("CVar '%s' already registered! Duplicate names are not allowed.", name);
        }
        return false;
    }
    else // Registration is a go.
    {
        return true;
    }
}

CVar * CVarManagerImpl::registerCVarBool(const char * const name, const char * const description,
                                         const std::uint32_t flags, const bool initValue,
                                         CVarValueCompletionCallback completionCb)
{
    if (!validateCVarRegistration(name, flags, initValue, &CVar::getBoolValue))
    {
        return nullptr;
    }

    auto newVar = memAlloc<CVarBool>(1);
    construct(newVar, name, description, flags, initValue, CVarNumberRange<bool>(false, true), completionCb);
    registeredCVars.linkWithKey(newVar, name);
    return newVar;
}

CVar * CVarManagerImpl::registerCVarInt(const char * const name, const char * const description,
                                        const std::uint32_t flags, const std::int64_t initValue,
                                        const std::int64_t minValue, const std::int64_t maxValue,
                                        CVarValueCompletionCallback completionCb)
{
    if (!validateCVarRegistration(name, flags, initValue, &CVar::getIntValue))
    {
        return nullptr;
    }

    auto newVar = memAlloc<CVarInt>(1);
    construct(newVar, name, description, flags, initValue, CVarNumberRange<std::int64_t>(minValue, maxValue), completionCb);
    registeredCVars.linkWithKey(newVar, name);
    return newVar;
}

CVar * CVarManagerImpl::registerCVarFloat(const char * const name, const char * const description,
                                          const std::uint32_t flags, const double initValue,
                                          const double minValue, const double maxValue,
                                          CVarValueCompletionCallback completionCb)
{
    if (!validateCVarRegistration(name, flags, initValue, &CVar::getFloatValue))
    {
        return nullptr;
    }

    auto newVar = memAlloc<CVarFloat>(1);
    construct(newVar, name, description, flags, initValue, CVarNumberRange<double>(minValue, maxValue), completionCb);
    registeredCVars.linkWithKey(newVar, name);
    return newVar;
}

CVar * CVarManagerImpl::registerCVarString(const char * const name, const char * const description,
                                           const std::uint32_t flags, const std::string & initValue,
                                           const char ** allowedStrings, CVarValueCompletionCallback completionCb)
{
    if (!validateCVarRegistration(name, flags, initValue, &CVar::getStringValue))
    {
        return nullptr;
    }

    auto newVar = memAlloc<CVarString>(1);
    construct(newVar, name, description, flags, initValue, CVarAllowedStrings(allowedStrings), completionCb);
    registeredCVars.linkWithKey(newVar, name);
    return newVar;
}

CVar * CVarManagerImpl::registerCVarEnum(const char * const name, const char * const description,
                                         const std::uint32_t flags, const std::int64_t initValue,
                                         const std::int64_t * const enumConstants, const char ** constNames,
                                         CVarValueCompletionCallback completionCb)
{
    if (!validateCVarRegistration(name, flags, initValue, &CVar::getIntValue))
    {
        return nullptr;
    }

    CVarEnumConst enumValue = { "", initValue };
    CVarEnumConstList enumConstList(enumConstants, constNames);

    // Find the name from provided values and const names:
    if (enumConstList.names != nullptr && enumConstList.values != nullptr)
    {
        for (int c = 0; enumConstList.names[c] != nullptr; ++c)
        {
            if (enumConstList.values[c] == initValue)
            {
                enumValue.name = enumConstList.names[c];
                break;
            }
        }
    }

    auto newVar = memAlloc<CVarEnum>(1);
    construct(newVar, name, description, flags, enumValue, enumConstList, completionCb);
    registeredCVars.linkWithKey(newVar, name);
    return newVar;
}

bool CVarManagerImpl::getCVarValueBool(const char * const name) const
{
    if (const auto cvar = findCVar(name))
    {
        return cvar->getBoolValue();
    }
    errorF("CVar '%s' not found.", name);
    return false;
}

std::int64_t CVarManagerImpl::getCVarValueInt(const char * const name) const
{
    if (const auto cvar = findCVar(name))
    {
        return cvar->getIntValue();
    }
    errorF("CVar '%s' not found.", name);
    return 0;
}

double CVarManagerImpl::getCVarValueFloat(const char * const name) const
{
    if (const auto cvar = findCVar(name))
    {
        return cvar->getFloatValue();
    }
    errorF("CVar '%s' not found.", name);
    return 0.0;
}

std::string CVarManagerImpl::getCVarValueString(const char * const name) const
{
    std::string str;
    if (const auto cvar = findCVar(name))
    {
        str = cvar->getStringValue();
        return str;
    }
    errorF("CVar '%s' not found.", name);
    return str;
}

CVar * CVarManagerImpl::setCVarValueBool(const char * const name, const bool value, const std::uint32_t flags)
{
    if (auto cvar = findCVar(name))
    {
        cvar->setBoolValue(value);
        return cvar;
    }
    return registerCVarBool(name, "", flags, value);
}

CVar * CVarManagerImpl::setCVarValueInt(const char * const name, const std::int64_t value, const std::uint32_t flags)
{
    if (auto cvar = findCVar(name))
    {
        cvar->setIntValue(value);
        return cvar;
    }
    return registerCVarInt(name, "", flags, value, INT64_MIN, INT64_MAX);
}

CVar * CVarManagerImpl::setCVarValueFloat(const char * const name, const double value, const std::uint32_t flags)
{
    if (auto cvar = findCVar(name))
    {
        cvar->setFloatValue(value);
        return cvar;
    }
    return registerCVarFloat(name, "", flags, value, DBL_MIN, DBL_MAX);
}

CVar * CVarManagerImpl::setCVarValueString(const char * const name, const std::string & value, const std::uint32_t flags)
{
    if (auto cvar = findCVar(name))
    {
        cvar->setStringValue(value);
        return cvar;
    }
    return registerCVarString(name, "", flags, value, nullptr);
}

bool CVarManagerImpl::internalSetStringValue(CVar * cvar, std::string value)
{
    CFG_ASSERT(cvar != nullptr);

    if (!cvar->isWritable() && (allowWritingRomCVars || allowWritingInitCVars))
    {
        return static_cast<CVarImplBase *>(cvar)->setStringValueIgnoreRO(std::move(value),
                                             allowWritingRomCVars, allowWritingInitCVars);
    }
    else
    {
        return cvar->setStringValue(std::move(value));
    }
}

bool CVarManagerImpl::internalSetDefaultValue(CVar * cvar)
{
    CFG_ASSERT(cvar != nullptr);

    if (!cvar->isWritable() && (allowWritingRomCVars || allowWritingInitCVars))
    {
        return static_cast<CVarImplBase *>(cvar)->setDefaultValueIgnoreRO(
                             allowWritingRomCVars, allowWritingInitCVars);
    }
    else
    {
        return cvar->setDefaultValue();
    }
}

void CVarManagerImpl::setAllowWritingReadOnlyVars(const bool allow) noexcept
{
    allowWritingRomCVars  = allow;
    allowWritingInitCVars = allow;
    // When ReadOnly CVars are set to writable, InitOnly vars
    // inherit the setting. The opposite is not true.
}

void CVarManagerImpl::setAllowWritingInitOnlyVars(const bool allow) noexcept
{
    allowWritingInitCVars = allow;
}

// ========================================================
// Virtual destructors anchored to this file:
// ========================================================

CVar::~CVar()
{ }

CVarImplBase::~CVarImplBase()
{ }

CVarManager::~CVarManager()
{ }

// ================================================================================================
//
//                                      Console Commands
//
// ================================================================================================

// ========================================================
// CommandArgs implementation:
// ========================================================

CommandArgs::CommandArgs()
    : CommandArgs(nullptr)
{ }

CommandArgs::CommandArgs(const char * const cmdStr)
    : argCount(0)
    , nextTokenIndex(0)
    , cmdName(nullptr)
{
    clearArray(argStrings);
    clearArray(tokenizedArgStr);

    if (cmdStr != nullptr)
    {
        parseArgString(cmdStr);
    }
}

CommandArgs::CommandArgs(const int argc, const char * argv[])
    : CommandArgs(nullptr)
{
    CFG_ASSERT(argc >= 1);
    CFG_ASSERT(argv != nullptr);

    // Command/prog name:
    cmdName = appendToken(argv[0], lengthOfString(argv[0]));

    for (int i = 1; i < argc; ++i)
    {
        const char * argStr = appendToken(argv[i], lengthOfString(argv[i]));
        if (!addArgString(argStr))
        {
            break;
        }
    }
}

CommandArgs::CommandArgs(const CommandArgs & other)
{
    *this = other;
}

CommandArgs & CommandArgs::operator = (const CommandArgs & other)
{
    if (this == &other)
    {
        return *this;
    }

    // Reset the current:
    argCount       = 0;
    nextTokenIndex = 0;

    clearArray(argStrings);
    clearArray(tokenizedArgStr);

    cmdName = appendToken(other.getCommandName(), lengthOfString(other.getCommandName()));

    for (int i = 0; i < other.getArgCount(); ++i)
    {
        const char * argStr = appendToken(other.getArgAt(i),
                                          lengthOfString(other.getArgAt(i)));

        // Don't bother checking since if all arguments fit in
        // the source CommandArgs, they must fit in here too!
        addArgString(argStr);
    }

    return *this;
}

const char * CommandArgs::getCommandName() const noexcept
{
    return cmdName;
}

int CommandArgs::getArgCount() const noexcept
{
    return argCount;
}

bool CommandArgs::isEmpty() const noexcept
{
    return argCount == 0;
}

const char * CommandArgs::operator[](const int index) const
{
    return getArgAt(index);
}

const char * CommandArgs::getArgAt(const int index) const
{
    CFG_ASSERT(index >= 0 && index < argCount);
    CFG_ASSERT(argStrings[index] != nullptr);
    return argStrings[index];
}

const char * const * CommandArgs::begin() const noexcept
{
    return argStrings;
}

const char * const * CommandArgs::end() const noexcept
{
    return argStrings + argCount;
}

int CommandArgs::compare(const int argIndex, const char * const str) const
{
    CFG_ASSERT(str != nullptr);
    if (argIndex < 0 || argIndex >= argCount)
    {
        return -1;
    }
    return std::strcmp(getArgAt(argIndex), str);
}

void CommandArgs::parseArgString(const char * argStr)
{
    CFG_ASSERT(argStr != nullptr);

    // First argument should be the unquoted command/program name.
    // Command string might be preceded by whitespace.

    int          argLen      = 0;
    int          quoteCount  = 0;
    bool         firstArg    = true;
    bool         quoted      = false;
    bool         singleQuote = false;
    bool         done        = false;
    const char * newArg      = nullptr;
    const char * argStart    = nullptr;

    for (; *argStr != '\0' && !done; ++argStr)
    {
        switch (*argStr)
        {
        // Quotes:
        //  Indicate that this arg won't end until
        //  the next enclosing quote is found.
        case '"' :
            {
                ++quoteCount;
                quoted = quoteCount & 1;
                if (argStart == nullptr)
                {
                    argStart = argStr;
                }
                break;
            }
        // Simple quotes are allowed to start an argument string
        // and can also appear inside a double-quoted block.
        case '\'' :
            {
                if (!quoted)
                {
                    ++quoteCount;
                    quoted = quoteCount & 1;
                    singleQuote = true;
                }
                else if (singleQuote)
                {
                    ++quoteCount;
                    quoted = quoteCount & 1;
                    singleQuote = false;
                }

                if (argStart == nullptr)
                {
                    argStart = argStr;
                }
                break;
            }
        // Whitespace:
        //  Separate each individual argument of a command
        //  line if we are not inside a quoted string.
        case  ' ' :
        case '\t' :
        case '\n' :
        case '\r' :
            {
                if (!quoted && argStart != nullptr)
                {
                    argLen = static_cast<int>(argStr - argStart);
                    newArg = appendToken(argStart, argLen);
                    argStart = nullptr;
                    if (firstArg)
                    {
                        cmdName = newArg;
                        firstArg = false;
                    }
                    else
                    {
                        if (!addArgString(newArg))
                        {
                            done = true; // Arg limit reached.
                        }
                    }
                }
                break;
            }
        // Start of a new argument or continuing an already started one.
        default :
            {
                if (argStart == nullptr)
                {
                    argStart = argStr;
                }
                break;
            }
        } // switch (*argStr)
    }

    // End reached with an open quote?
    // We don't trash the arguments completely in such case. Some parameters
    // might still be valid. Failing will be up to the command handler.
    if (quoted)
    {
        errorF("Attention! Command string ended with open quotation block!");
        // Allow it to proceed.
    }

    // Last residual argument before the end of the string.
    if (argStart != nullptr)
    {
        argLen = static_cast<int>(argStr - argStart);
        newArg = appendToken(argStart, argLen);
        if (firstArg)
        {
            cmdName = newArg;
        }
        else
        {
            addArgString(newArg);
        }
    }
}

bool CommandArgs::addArgString(const char * const argStr)
{
    if (argStr == nullptr)
    {
        return errorF("Null token! CommandArgs::tokenizedArgStr[] depleted?");
    }
    else if (argCount == MaxCommandArguments)
    {
        return errorF("Too many arguments! Ignoring extraneous ones...");
    }
    else
    {
        argStrings[argCount++] = argStr;
        return true;
    }
}

const char * CommandArgs::appendToken(const char * token, int tokenLen)
{
    if ((nextTokenIndex + tokenLen) >= MaxCommandArgStrLength)
    {
        errorF("Command argument string too long! Max is %i characters.", MaxCommandArgStrLength - 1);
        return nullptr;
    }

    // If the token is enclosed in single or double quotes, ignore them.
    // Note that this assumes the opening AND closing quotes are present!
    char * outTokenPtr = &tokenizedArgStr[nextTokenIndex];
    if (token[0] == '"' || token[0] == '\'')
    {
        ++token;
        tokenLen -= 2;
    }

    std::memcpy(outTokenPtr, token, tokenLen);
    nextTokenIndex += (tokenLen + 1);
    outTokenPtr[tokenLen] = '\0';

    return outTokenPtr;
}

// ========================================================
// class CommandImplBase:
// ========================================================

#if CFG_PACK_STRUCTURES
    #pragma pack(push, 1)
#endif // CFG_PACK_STRUCTURES

class CommandImplBase
    : public HashTableLink<CommandImplBase>
    , public Command
{
public:

    // Not copyable.
    CommandImplBase(const CommandImplBase &) = delete;
    CommandImplBase & operator = (const CommandImplBase &) = delete;

    // Construct with common parameters. Description may be null or an empty string.
    // Min and max args may be negative to disable CommandManager-side argument count
    // validation. Flags are any user defined set of integer bit-flags, including zero.
    CommandImplBase(const char * cmdName,
                    const char * cmdDesc,
                    std::uint32_t cmdFlags,
                    int minCmdArgs,
                    int maxCmdArgs);

    virtual ~CommandImplBase() = default;
    virtual void onExecute(const CommandArgs & args);
    virtual bool isAlias() const override;
    virtual int argumentCompletion(const char  * partialArg,
                                   std::string * outMatches,
                                   int maxMatches) const override;

    std::uint32_t getFlags() const override;
    void setFlags(std::uint32_t newFlags) override;

    const char * getNameCString() const override;
    const char * getDescCString() const override;

    std::string getName() const override;
    std::string getDesc() const override;

    int getMinArgs() const override;
    int getMaxArgs() const override;

private:

    // Opaque user defined bitflags. Can be changed after construction.
    std::uint32_t flags;

    // We don't need the full range of an integer for the arg counts.
    // A command will have at most half a dozen args, in the rare case.
    const std::int8_t minArgs;
    const std::int8_t maxArgs;

    // A name must always be provided. Description may be an empty string.
    char name[MaxCommandNameLength];
    char desc[MaxCommandDescLength];
};

#if CFG_PACK_STRUCTURES
    #pragma pack(pop)
#endif // CFG_PACK_STRUCTURES

// ========================================================
// CommandImplBase implementation:
// ========================================================

CommandImplBase::CommandImplBase(const char * const cmdName,
                                 const char * const cmdDesc,
                                 const std::uint32_t cmdFlags,
                                 const int minCmdArgs,
                                 const int maxCmdArgs)
    : flags(cmdFlags)
    , minArgs(static_cast<std::int8_t>(minCmdArgs))
    , maxArgs(static_cast<std::int8_t>(maxCmdArgs))
{
    CFG_ASSERT(minCmdArgs <= MaxCommandArguments);
    CFG_ASSERT(maxCmdArgs <= MaxCommandArguments);

    CFG_ASSERT(cmdName != nullptr && *cmdName != '\0');
    CFG_ASSERT(lengthOfString(cmdName) < MaxCommandNameLength);

    clearArray(name);
    clearArray(desc);
    copyString(name, lengthOfArray(name), cmdName);

    // Description comment is optional.
    if (cmdDesc != nullptr && *cmdDesc != '\0')
    {
        CFG_ASSERT(lengthOfString(cmdDesc) < MaxCommandDescLength);
        copyString(desc, lengthOfArray(desc), cmdDesc);
    }
}

void CommandImplBase::onExecute(const CommandArgs & /* args */)
{
    // Default no-op command handler.
}

bool CommandImplBase::isAlias() const
{
    return false;
}

int CommandImplBase::argumentCompletion(const char  * /* partialArg */,
                                        std::string * /* outMatches */,
                                        int /* maxMatches */) const
{
    // Default no-op completion handler.
    return 0;
}

std::uint32_t CommandImplBase::getFlags() const
{
    return flags;
}

void CommandImplBase::setFlags(const std::uint32_t newFlags)
{
    flags = newFlags;
}

const char * CommandImplBase::getNameCString() const
{
    return name;
}

const char * CommandImplBase::getDescCString() const
{
    return desc;
}

std::string CommandImplBase::getName() const
{
    return name;
}

std::string CommandImplBase::getDesc() const
{
    return desc;
}

int CommandImplBase::getMinArgs() const
{
    return minArgs;
}

int CommandImplBase::getMaxArgs() const
{
    return maxArgs;
}

// ========================================================
// class CommandImplCallbacks:
// ========================================================

//
// Wraps a pair of C-style function pointers (callbacks) into a
// CommandImpl that we can insert in the commands hash table.
//
class CommandImplCallbacks final
    : public CommandImplBase
{
public:

    CommandImplCallbacks(const char * cmdName,
                         const char * cmdDesc,
                         std::uint32_t cmdFlags,
                         int minCmdArgs,
                         int maxCmdArgs,
                         CommandHandlerCallback execCb,
                         CommandArgCompletionCallback argCompleteCb,
                         void * userCtx);

    void onExecute(const CommandArgs & args) override;
    int argumentCompletion(const char  * partialArg,
                           std::string * outMatches,
                           int maxMatches) const override;

private:

    CommandHandlerCallback       execCallback;
    CommandArgCompletionCallback argCompletionCallback;
    void *                       userContext;
};

CommandImplCallbacks::CommandImplCallbacks(const char * const cmdName,
                                           const char * const cmdDesc,
                                           const std::uint32_t cmdFlags,
                                           const int minCmdArgs,
                                           const int maxCmdArgs,
                                           CommandHandlerCallback execCb,
                                           CommandArgCompletionCallback argCompleteCb,
                                           void * userCtx)
    : CommandImplBase(cmdName, cmdDesc, cmdFlags, minCmdArgs, maxCmdArgs)
    , execCallback(execCb)
    , argCompletionCallback(argCompleteCb)
    , userContext(userCtx)
{
}

void CommandImplCallbacks::onExecute(const CommandArgs & args)
{
    if (execCallback != nullptr)
    {
        execCallback(args, userContext);
    }
}

int CommandImplCallbacks::argumentCompletion(const char  * const partialArg,
                                             std::string * outMatches,
                                             const int maxMatches) const
{
    if (argCompletionCallback != nullptr)
    {
        return argCompletionCallback(partialArg, outMatches, maxMatches, userContext);
    }
    return 0;
}

// ========================================================
// class CommandImplDelegates:
// ========================================================

//
// Wraps a pair of std::functions (delegates) into a
// CommandImpl that we can insert in the commands hash table.
//
class CommandImplDelegates final
    : public CommandImplBase
{
public:

    CommandImplDelegates(const char * cmdName,
                         const char * cmdDesc,
                         std::uint32_t cmdFlags,
                         int minCmdArgs,
                         int maxCmdArgs,
                         CommandHandlerDelegate execDl,
                         CommandArgCompletionDelegate argComplDl);

    void onExecute(const CommandArgs & args) override;
    int argumentCompletion(const char * partialArg, std::string * outMatches, int maxMatches) const override;

private:

    CommandHandlerDelegate       execDelegate;
    CommandArgCompletionDelegate argCompletionDelegate;
};

CommandImplDelegates::CommandImplDelegates(const char * const cmdName,
                                           const char * const cmdDesc,
                                           const std::uint32_t cmdFlags,
                                           const int minCmdArgs,
                                           const int maxCmdArgs,
                                           CommandHandlerDelegate execDl,
                                           CommandArgCompletionDelegate argCompleteDl)
    : CommandImplBase(cmdName, cmdDesc, cmdFlags, minCmdArgs, maxCmdArgs)
    , execDelegate(std::move(execDl))
    , argCompletionDelegate(std::move(argCompleteDl))
{
}

void CommandImplDelegates::onExecute(const CommandArgs & args)
{
    if (execDelegate != nullptr)
    {
        execDelegate(args);
    }
}

int CommandImplDelegates::argumentCompletion(const char  * const partialArg,
                                             std::string * outMatches,
                                             const int maxMatches) const
{
    if (argCompletionDelegate != nullptr)
    {
        return argCompletionDelegate(partialArg, outMatches, maxMatches);
    }
    return 0;
}

// ========================================================
// class CommandImplMemberFuncs:
// ========================================================

//
// Wraps a pair of class methods plus a pointer to the
// owning object (the "this pointer") allowing a standard
// C++ class registering a member method as a command handler.
//
class CommandImplMemberFuncs final
: public CommandImplBase
{
public:

    CommandImplMemberFuncs(const char * cmdName,
                           const char * cmdDesc,
                           std::uint32_t cmdFlags,
                           int minCmdArgs,
                           int maxCmdArgs,
                           CommandHandlerMemFunc execMf,
                           CommandHandlerMemFunc argCompleteMf);

    void onExecute(const CommandArgs & args) override;
    int argumentCompletion(const char * partialArg, std::string * outMatches, int maxMatches) const override;

private:

    CommandHandlerMemFunc execMemFunc;
    CommandHandlerMemFunc argCompletionMemFunc;
};

CommandImplMemberFuncs::CommandImplMemberFuncs(const char * const cmdName,
                                               const char * const cmdDesc,
                                               const std::uint32_t cmdFlags,
                                               const int minCmdArgs,
                                               const int maxCmdArgs,
                                               CommandHandlerMemFunc execMf,
                                               CommandHandlerMemFunc argCompleteMf)
    : CommandImplBase(cmdName, cmdDesc, cmdFlags, minCmdArgs, maxCmdArgs)
    , execMemFunc(execMf)
    , argCompletionMemFunc(argCompleteMf)
{
}

void CommandImplMemberFuncs::onExecute(const CommandArgs & args)
{
    if (execMemFunc != nullptr)
    {
        execMemFunc.invoke<void>(args);
    }
}

int CommandImplMemberFuncs::argumentCompletion(const char  * const partialArg,
                                               std::string * outMatches,
                                               const int maxMatches) const
{
    if (argCompletionMemFunc != nullptr)
    {
        return argCompletionMemFunc.invoke<int>(partialArg, outMatches, maxMatches);
    }
    return 0;
}

// ========================================================
// class CommandImplAlias:
// ========================================================

//
// Helper used to define command aliases using the same
// CommandImpl interface. It just stores a copy of the
// aliased command string. onExecute() pushes this string
// into the CommandManager buffer.
//
class CommandImplAlias final
    : public CommandImplBase
{
public:

    CommandImplAlias(const char * cmdName,
                     const char * cmdDesc,
                     const char * cmdStr,
                     CommandExecMode cmdExec,
                     CommandManager * cmdMgr);

    ~CommandImplAlias();
    void onExecute(const CommandArgs & args) override;
    bool isAlias() const override;
    char * toCfgString(char * outBuffer, int bufferSize) const;

private:

    const CommandExecMode execMode;
    CommandManager *      manager;
    char *                targetCommand;
};

CommandImplAlias::CommandImplAlias(const char * const cmdName,
                                   const char * const cmdDesc,
                                   const char * const cmdStr,
                                   const CommandExecMode cmdExec,
                                   CommandManager * cmdMgr)
    : CommandImplBase(cmdName, cmdDesc, 0, 0, 0)
    , execMode(cmdExec)
    , manager(cmdMgr)
    , targetCommand(cloneString(cmdStr))
{
    CFG_ASSERT(cmdMgr != nullptr);
}

CommandImplAlias::~CommandImplAlias()
{
    memFree(targetCommand);
}

void CommandImplAlias::onExecute(const CommandArgs & /* args */)
{
    manager->execute(execMode, targetCommand);
}

bool CommandImplAlias::isAlias() const
{
    return true;
}

char * CommandImplAlias::toCfgString(char * outBuffer, const int bufferSize) const
{
    CFG_ASSERT(outBuffer != nullptr);

    const char * description = getDescCString();
    const char * modeFlag;

    if (execMode == CommandExecMode::Insert)
    {
        modeFlag = "-insert";
    }
    else if (execMode == CommandExecMode::Immediate)
    {
        modeFlag = "-immediate";
    }
    else // CommandExecMode::Append
    {
        modeFlag = "-append";
    }

    // alias  name  "cmd-string"  mode-flag  "optional-description"
    if (*description != '\0')
    {
        std::snprintf(outBuffer, bufferSize,
                      "alias %s \"%s\" %s \"%s\"",
                      getNameCString(), targetCommand, modeFlag, description);
    }
    else
    {
        std::snprintf(outBuffer, bufferSize,
                      "alias %s \"%s\" %s",
                      getNameCString(), targetCommand, modeFlag);
    }
    return outBuffer;
}

// ========================================================
// Command name string comparisons and sorting:
// ========================================================

static inline int cmdCmpNames(const char * const a, const char * const b, const std::uint32_t count = ~0u)
{
    #if CFG_COMMAND_CASE_SENSITIVE_NAMES
    return std::strncmp(a, b, count);
    #else // !CFG_COMMAND_CASE_SENSITIVE_NAMES
    return compareStringsNoCase(a, b, count);
    #endif // CFG_COMMAND_CASE_SENSITIVE_NAMES
}

static inline bool cmdNameStartsWith(const char * const name, const char * const prefix)
{
    CFG_ASSERT(name   != nullptr);
    CFG_ASSERT(prefix != nullptr);

    const int nameLen   = lengthOfString(name);
    const int prefixLen = lengthOfString(prefix);

    if (nameLen < prefixLen)
    {
        return false;
    }
    else if (nameLen == 0 || prefixLen == 0)
    {
        return false;
    }
    else
    {
        return cmdCmpNames(name, prefix, prefixLen) == 0;
    }
}

static inline void cmdSortMatches(Command ** outMatches, const int count)
{
    std::sort(outMatches, outMatches + count,
              [](const Command * const a, const Command * const b)
              {
                  return cmdCmpNames(a->getNameCString(), b->getNameCString()) < 0;
              });
}

static inline void cmdSortMatches(const char ** outMatches, const int count)
{
    std::sort(outMatches, outMatches + count,
              [](const char * const a, const char * const b)
              {
                  return cmdCmpNames(a, b) < 0;
              });
}

// ========================================================
// class CommandManagerImpl:
// ========================================================

class CommandManagerImpl final
    : public CommandManager
{
public:

    // Not copyable.
    CommandManagerImpl(const CommandManagerImpl &) = delete;
    CommandManagerImpl & operator = (const CommandManagerImpl &) = delete;

    CommandManagerImpl(int hashTableSize, CVarManager * cvarMgr);
    ~CommandManagerImpl();

    Command * findCommand(const char * name) const override;
    int findCommandsWithPartialName(const char * partialName, Command ** outMatches, int maxMatches) const override;
    int findCommandsWithPartialName(const char * partialName, const char ** outMatches, int maxMatches) const override;
    int findCommandsWithFlags(std::uint32_t flags, Command ** outMatches, int maxMatches) const override;

    bool removeCommand(const char * name) override;
    bool removeCommand(Command * cmd) override;
    bool removeCommandAlias(const char * aliasName) override;
    void removeAllCommands() override;
    void removeAllCommandAliases() override;

    int getRegisteredCommandsCount() const override;
    int getCommandAliasCount() const override;
    bool isValidCommandName(const char * name) const override;
    void enumerateAllCommands(CommandEnumerateCallback enumCallback, void * userContext) override;

    void disableCommandsWithFlags(std::uint32_t flags) override;
    void enableAllCommands() override;

    CVarManager * getCVarManager() const override;
    void setCVarManager(CVarManager * cvarMgr) override;

    bool registerCommand(const char * name,
                         CommandHandlerCallback handler,
                         CommandArgCompletionCallback completionHandler = nullptr,
                         void * userContext       = nullptr,
                         const char * description = "",
                         std::uint32_t flags      =  0,
                         int minArgs              = -1,
                         int maxArgs              = -1) override;

    bool registerCommand(const char * name,
                         CommandHandlerDelegate handler,
                         CommandArgCompletionDelegate completionHandler = nullptr,
                         const char * description = "",
                         std::uint32_t flags      =  0,
                         int minArgs              = -1,
                         int maxArgs              = -1) override;

    bool registerCommand(const char * name,
                         CommandHandlerMemFunc handler,
                         CommandHandlerMemFunc completionHandler = nullptr,
                         const char * description = "",
                         std::uint32_t flags      =  0,
                         int minArgs              = -1,
                         int maxArgs              = -1) override;

    bool createCommandAlias(const char * aliasName,
                            const char * aliasedCmdStr,
                            CommandExecMode execMode,
                            const char * description = "") override;

    void execNow(const char * str)    override;
    void execInsert(const char * str) override;
    void execAppend(const char * str) override;
    void execute(CommandExecMode execMode, const char * str) override;
    int  execBufferedCommands(std::uint32_t maxCommandsToExec = ExecAll) override;
    bool execConfigFile(const char * filename, SimpleCommandTerminal * term) override;
    void execStartupCommandLine(int argc, const char * argv[]) override;
    bool hasBufferedCommands() const override;

private:

    template<typename T>
    int findWithPartialNameHelper(const char * partialName, T * outMatches,
                                  int maxMatches, T (*pGetCmd)(Command *)) const;

    void execTokenized(const CommandArgs & cmdArgs);
    bool registerCmdPreValidate(const char * cmdName) const;

    bool extractNextCommand(const char ** outStr, char * destBuf,
                            int destSizeInChars, bool * outOverflowed) const;

    #if CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION
    bool expandCVar(const char ** outStr, int * outCharsCopied, char * destBuf,
                    int destSizeInChars, int recursionDepth) const;
    #endif // CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION

    //
    // Hash function used to lookup command names in the HT.
    // Can be case-sensitive or not.
    //
    #if CFG_COMMAND_CASE_SENSITIVE_NAMES
    using CommandNameHasher = StringHasher;
    #else // !CFG_COMMAND_CASE_SENSITIVE_NAMES
    using CommandNameHasher = StringHasherNoCase;
    #endif // CFG_COMMAND_CASE_SENSITIVE_NAMES

    // All the registered commands in a hash table for fast lookup by name.
    LinkedHashTable<CommandImplBase, CommandNameHasher> registeredCommands;

    // Optional pointer to a CVarManager to provided CVar name expansion and command-style CVar updating.
    CVarManagerImpl * cvarManager;

    // Commands matching this set of flags will not be allowed to execute.
    // Zero allows execution of all commands. 'DisableAll' prevents all commands from executing.
    std::uint32_t disabledCmdFlags;

    // Number of registered CommandImplAlias.
    int cmdAliasCount;

    // Chars used so far in the cmdBuffer. Up to CommandBufferSize-1,
    // since the max size must also accommodate a null terminator at the end.
    int cmdBufferUsed;

    // Buffered commands. Huge string with individual
    // command+args blocks separated by a semicolon (;).
    char cmdBuffer[CommandBufferSize];
};

// ========================================================
// CommandManagerImpl implementation:
// ========================================================

CommandManager * CommandManager::createInstance(const int cmdHashTableSizeHint, CVarManager * cvarMgr)
{
    auto cmdManager = memAlloc<CommandManagerImpl>(1);
    return construct(cmdManager, cmdHashTableSizeHint, cvarMgr);
}

void CommandManager::destroyInstance(CommandManager * cmdManager)
{
    destroy(cmdManager);
    memFree(cmdManager);
}

CommandManagerImpl::CommandManagerImpl(const int hashTableSize, CVarManager * cvarMgr)
    : cvarManager(static_cast<CVarManagerImpl *>(cvarMgr))
    , disabledCmdFlags(0)
    , cmdAliasCount(0)
    , cmdBufferUsed(0)
{
    if (hashTableSize > 0)
    {
        registeredCommands.allocate(hashTableSize);
    }
    clearArray(cmdBuffer);
}

CommandManagerImpl::~CommandManagerImpl()
{
    auto cmd = registeredCommands.getFirst();
    while (cmd != nullptr)
    {
        auto temp = cmd->getNext();
        destroy(cmd);
        memFree(cmd);
        cmd = temp;
    }
}

Command * CommandManagerImpl::findCommand(const char * const name) const
{
    if (name == nullptr || *name == '\0')
    {
        return nullptr;
    }
    return registeredCommands.findByKey(name);
}

int CommandManagerImpl::findCommandsWithPartialName(const char * const partialName,
                                                    Command ** outMatches, const int maxMatches) const
{
    return findWithPartialNameHelper<Command *>(partialName, outMatches, maxMatches,
                                                [](Command * cmd) { return cmd; });
}

int CommandManagerImpl::findCommandsWithPartialName(const char * const partialName,
                                                    const char ** outMatches, const int maxMatches) const
{
    return findWithPartialNameHelper<const char *>(partialName, outMatches, maxMatches,
                                                   [](Command * cmd) { return cmd->getNameCString(); });
}

int CommandManagerImpl::findCommandsWithFlags(const std::uint32_t flags,
                                              Command ** outMatches, const int maxMatches) const
{
    if (flags == 0)
    {
        return 0;
    }
    else if (outMatches == nullptr || maxMatches <= 0)
    {
        return -1;
    }

    int matchesFound = 0;

    // Partial name searching unfortunately can't take advantage of the optimized
    // constant time hash table lookup, so we have to fall back to a linear search.
    for (auto cmd = registeredCommands.getFirst(); cmd; cmd = cmd->getNext())
    {
        if (cmd->getFlags() & flags)
        {
            if (matchesFound < maxMatches)
            {
                outMatches[matchesFound] = cmd;
            }
            ++matchesFound; // Keep incrementing even if outMatches[] is full,
                            // so the caller can know the total num found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        cmdSortMatches(outMatches, std::min(matchesFound, maxMatches));
    }
    return matchesFound;
}

template<typename T>
int CommandManagerImpl::findWithPartialNameHelper(const char * const partialName, T * outMatches,
                                                  const int maxMatches, T (*pGetCmd)(Command *)) const
{
    if (partialName == nullptr || *partialName == '\0')
    {
        return 0;
    }
    else if (outMatches == nullptr || maxMatches <= 0)
    {
        return -1;
    }

    int matchesFound = 0;

    // Partial name searching unfortunately can't take advantage of the optimized
    // constant time hash table lookup, so we have to fall back to a linear search.
    for (auto cmd = registeredCommands.getFirst(); cmd; cmd = cmd->getNext())
    {
        if (cmdNameStartsWith(cmd->getNameCString(), partialName))
        {
            if (matchesFound < maxMatches)
            {
                outMatches[matchesFound] = pGetCmd(cmd);
            }
            ++matchesFound; // Keep incrementing even if outMatches[] is full,
                            // so the caller can know the total num found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        cmdSortMatches(outMatches, std::min(matchesFound, maxMatches));
    }
    return matchesFound;
}

bool CommandManagerImpl::removeCommand(const char * const name)
{
    if (!isValidCommandName(name))
    {
        return errorF("'%s' is not a valid command name! Nothing to remove.", name);
    }

    auto cmd = registeredCommands.unlinkByKey(name);
    if (cmd == nullptr)
    {
        return false; // No such command/alias.
    }

    destroy(cmd);
    memFree(cmd);
    return true;
}

bool CommandManagerImpl::removeCommand(Command * cmd)
{
    if (cmd == nullptr)
    {
        return false;
    }
    return removeCommand(cmd->getNameCString());
}

bool CommandManagerImpl::removeCommandAlias(const char * const aliasName)
{
    if (!isValidCommandName(aliasName))
    {
        return false;
    }

    const auto cmd = registeredCommands.findByKey(aliasName);
    if (cmd == nullptr || !cmd->isAlias())
    {
        return false;
    }

    if (!removeCommand(aliasName))
    {
        return false;
    }

    --cmdAliasCount;
    return true;
}

void CommandManagerImpl::removeAllCommands()
{
    auto cmd = registeredCommands.getFirst();
    while (cmd != nullptr)
    {
        auto temp = cmd->getNext();
        destroy(cmd);
        memFree(cmd);
        cmd = temp;
    }
    registeredCommands.deallocate();
}

void CommandManagerImpl::removeAllCommandAliases()
{
    std::vector<CommandImplBase *> aliases;

    for (auto cmd = registeredCommands.getFirst(); cmd; cmd = cmd->getNext())
    {
        if (cmd->isAlias())
        {
            aliases.push_back(cmd);
        }
    }

    for (auto alias : aliases)
    {
        removeCommand(alias);
    }

    cmdAliasCount = 0;
}

int CommandManagerImpl::getRegisteredCommandsCount() const
{
    return registeredCommands.getSize();
}

int CommandManagerImpl::getCommandAliasCount() const
{
    return cmdAliasCount;
}

bool CommandManagerImpl::isValidCommandName(const char * const name) const
{
    //
    // Command names follow the C++ variable naming rules,
    // that is, cannot start with a number, cannot contain
    // white spaces, cannot start with nor contain any special
    // characters or punctuation, except for the underscore.
    //
    if (name == nullptr || *name == '\0')
    {
        return false;
    }

    int i = 0;
    int c = name[i++];

    // Must start with a letter or an underscore:
    if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c != '_'))
    {
        return false;
    }

    while (name[i] != '\0')
    {
        c = name[i++];

        // If not a number, lower or upper case letter
        // and not an underscore it's not a valid command name.
        if ((c < '0' || c > '9') &&
            (c < 'a' || c > 'z') &&
            (c < 'A' || c > 'Z') &&
            (c != '_'))
        {
            return false;
        }
    }

    // Lastly, must comply to our fixed max name length.
    if (i >= MaxCommandNameLength)
    {
        return false;
    }
    return true;
}

void CommandManagerImpl::enumerateAllCommands(CommandEnumerateCallback enumCallback, void * userContext)
{
    CFG_ASSERT(enumCallback != nullptr);
    for (auto cmd = registeredCommands.getFirst(); cmd; cmd = cmd->getNext())
    {
        if (!enumCallback(cmd, userContext))
        {
            return;
        }
    }
}

void CommandManagerImpl::disableCommandsWithFlags(const std::uint32_t flags)
{
    disabledCmdFlags = flags;
}

void CommandManagerImpl::enableAllCommands()
{
    disabledCmdFlags = 0;
}

CVarManager * CommandManagerImpl::getCVarManager() const
{
    return cvarManager;
}

void CommandManagerImpl::setCVarManager(CVarManager * cvarMgr)
{
    cvarManager = static_cast<CVarManagerImpl *>(cvarMgr);
}

bool CommandManagerImpl::registerCmdPreValidate(const char * const cmdName) const
{
    if (!isValidCommandName(cmdName))
    {
        return errorF("Bad command name '%s'! Can't register it.", cmdName);
    }
    if (findCommand(cmdName)) // No duplicates allowed.
    {
        return errorF("Command '%s' already registered! Duplicate commands are not allowed.", cmdName);
    }
    if ((cvarManager != nullptr) && cvarManager->findCVar(cmdName)) // Avoid Cmd/CVar name ambiguities.
    {
        return errorF("A CVar named '%s' already exists. Cannot declare a new command with this name!", cmdName);
    }
    return true;
}

bool CommandManagerImpl::registerCommand(const char * const name,
                                         CommandHandlerCallback handler,
                                         CommandArgCompletionCallback completionHandler,
                                         void * userContext,
                                         const char * const description,
                                         const std::uint32_t flags,
                                         const int minArgs,
                                         const int maxArgs)
{
    if (handler == nullptr)
    {
        return errorF("No command handler provided!");
    }
    if (!registerCmdPreValidate(name))
    {
        return false;
    }

    auto newCmd = memAlloc<CommandImplCallbacks>(1);
    construct(newCmd, name, description, flags, minArgs, maxArgs, handler, completionHandler, userContext);

    registeredCommands.linkWithKey(newCmd, name);
    return true;
}

bool CommandManagerImpl::registerCommand(const char * const name,
                                         CommandHandlerDelegate handler,
                                         CommandArgCompletionDelegate completionHandler,
                                         const char * const description,
                                         const std::uint32_t flags,
                                         const int minArgs,
                                         const int maxArgs)
{
    if (handler == nullptr)
    {
        return errorF("No command handler provided!");
    }
    if (!registerCmdPreValidate(name))
    {
        return false;
    }

    auto newCmd = memAlloc<CommandImplDelegates>(1);
    construct(newCmd, name, description, flags, minArgs, maxArgs,
              std::move(handler), std::move(completionHandler));

    registeredCommands.linkWithKey(newCmd, name);
    return true;
}

bool CommandManagerImpl::registerCommand(const char * const name,
                                         CommandHandlerMemFunc handler,
                                         CommandHandlerMemFunc completionHandler,
                                         const char * const description,
                                         const std::uint32_t flags,
                                         const int minArgs,
                                         const int maxArgs)
{
    if (handler == nullptr)
    {
        return errorF("No command handler provided!");
    }
    if (!registerCmdPreValidate(name))
    {
        return false;
    }

    auto newCmd = memAlloc<CommandImplMemberFuncs>(1);
    construct(newCmd, name, description, flags, minArgs, maxArgs, handler, completionHandler);

    registeredCommands.linkWithKey(newCmd, name);
    return true;
}

bool CommandManagerImpl::createCommandAlias(const char * const aliasName,
                                            const char * const aliasedCmdStr,
                                            const CommandExecMode execMode,
                                            const char * const description)
{
    if (aliasedCmdStr == nullptr || *aliasedCmdStr == '\0')
    {
        return errorF("Can't create a command alias for an empty/null string!");
    }
    if (!isValidCommandName(aliasName))
    {
        return errorF("'%s' is not a valid alias or command name!", aliasName);
    }
    if (findCommand(aliasName))
    {
        return errorF("A command or alias named '%s' already exists!", aliasName);
    }
    if ((cvarManager != nullptr) && cvarManager->findCVar(aliasName))
    {
        return errorF("A CVar named '%s' already exists. Cannot declare a new command alias with this name!", aliasName);
    }

    auto newCmd = memAlloc<CommandImplAlias>(1);
    construct(newCmd, aliasName, description, aliasedCmdStr, execMode, this);

    registeredCommands.linkWithKey(newCmd, aliasName);
    ++cmdAliasCount;

    return true;
}

void CommandManagerImpl::execNow(const char * str)
{
    CFG_ASSERT(str != nullptr);
    if (*str == '\0')
    {
        return;
    }

    // Input string might consist of multiple commands.
    // Split it up and handle each command separately.
    bool overflowed;
    char tempBuffer[MaxCommandArgStrLength];
    while (extractNextCommand(&str, tempBuffer, lengthOfArray(tempBuffer), &overflowed))
    {
        if (overflowed)
        {
            // Malformed command line that won't fit in our buffers.
            // Discard the rest of the string.
            errorF("Discarding rest of command line due to malformed string...");
            break;
        }

        // Tokenize the command string, separating command name and splitting the args, then we can run it.
        CommandArgs cmdArgs(tempBuffer);
        execTokenized(cmdArgs);
    }
}

void CommandManagerImpl::execInsert(const char * const str)
{
    CFG_ASSERT(str != nullptr);
    if (*str == '\0')
    {
        return;
    }

    const int strLen = lengthOfString(str) + 1;

    // Check for buffer overflow:
    if ((cmdBufferUsed + strLen) >= CommandBufferSize)
    {
        errorF("Buffer overflow! Command buffer depleted in CommandManager::execInsert()!");
        return;
    }

    // Move the existing command text so we can prepend the new command:
    for (int i = cmdBufferUsed - 1; i >= 0; --i)
    {
        cmdBuffer[i + strLen] = cmdBuffer[i];
    }

    // Copy the new text in:
    std::memcpy(cmdBuffer, str, strLen - 1);

    // Separate the command strings:
    cmdBuffer[strLen - 1] = CommandTextSeparator;
    cmdBufferUsed += strLen;

    // Ensure always null terminated.
    cmdBuffer[cmdBufferUsed] = '\0';
}

void CommandManagerImpl::execAppend(const char * const str)
{
    CFG_ASSERT(str != nullptr);
    if (*str == '\0')
    {
        return;
    }

    const int strLen = lengthOfString(str) + 1;

    // Check for buffer overflow:
    if ((cmdBufferUsed + strLen) >= CommandBufferSize)
    {
        errorF("Buffer overflow! Command buffer depleted in CommandManager::execAppend()!");
        return;
    }

    // Copy the user command:
    std::memcpy(cmdBuffer + cmdBufferUsed, str, strLen - 1);
    cmdBufferUsed += strLen;

    // Separate the command strings and null terminate the buffer:
    cmdBuffer[cmdBufferUsed - 1] = CommandTextSeparator;
    cmdBuffer[cmdBufferUsed] = '\0';
}

void CommandManagerImpl::execute(const CommandExecMode execMode, const char * const str)
{
    switch (execMode)
    {
    case CommandExecMode::Immediate :
        execNow(str);
        break;
    case CommandExecMode::Insert :
        execInsert(str);
        break;
    case CommandExecMode::Append :
        execAppend(str);
        break;
    default :
        errorF("Invalid CommandExecMode enum value!");
    } // switch (execMode)
}

bool CommandManagerImpl::execConfigFile(const char * const filename, SimpleCommandTerminal * term)
{
    FileIOCallbacks * io = getFileIOCallbacks();

    FileHandle fileIn;
    if (!io->open(&fileIn, filename, FileOpenMode::Read))
    {
        return false;
    }

    int  lineNum = 0;
    char line[MaxCommandArgStrLength];

    // Scan the file line-by-line:
    while (!io->isAtEOF(fileIn) && io->readLine(fileIn, line, lengthOfArray(line)))
    {
        ++lineNum;

        // Skip blank lines and comments ('#')
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#')
        {
            continue;
        }
        // Allow C++-style single-line comments inside config files.
        else if (line[0] == '/' && line[1] == '/')
        {
            continue;
        }

        if (term != nullptr)
        {
            // Echo current line to console, adding source file and line number:
            const int lastChar = lengthOfString(line) - 1;
            if (line[lastChar] == '\n')
            {
                line[lastChar] = '\0'; // Get rid of the extra '\n'. We add one by default.
            }
            term->printF("%s(%i): %s\n", filename, lineNum, line);
        }

        // Execute immediately:
        execNow(line);
    }

    io->close(fileIn);
    return true;
}

void CommandManagerImpl::execStartupCommandLine(const int argc, const char * argv[])
{
    char   cmdline[MaxCommandArgStrLength] = {'\0'};
    char * ptr     = cmdline;
    int    length  = 0;
    bool   setCmd  = false;

    if (cvarManager != nullptr)
    {
        cvarManager->setAllowWritingInitOnlyVars(true);
    }

    // argv[0] assumed to be the program name.
    for (int i = 1; i < argc; ++i)
    {
        const char * arg = argv[i];
        if (*arg == '+')
        {
            if (length > 0)
            {
                if (setCmd)
                {
                    execNow(cmdline);
                }
                else
                {
                    execAppend(cmdline);
                }
            }

            ++arg;
            ptr = cmdline;
            length = 0;

            // 'set/reset' commands shall not be buffered when running the cmdline.
            setCmd = (cmdCmpNames(arg, "set") == 0) || (cmdCmpNames(arg, "reset") == 0);
        }

        if ((lengthOfArray(cmdline) - length - 1) <= 0)
        {
            break;
        }

        const int written = copyString(ptr, lengthOfArray(cmdline) - length, arg);
        ptr[written    ] = ' ';
        ptr[written + 1] = '\0';

        length += written + 1;
        ptr    += written + 1;
    }

    if (length > 0)
    {
        if (setCmd)
        {
            execNow(cmdline);
        }
        else
        {
            execAppend(cmdline);
        }
    }

    if (cvarManager != nullptr)
    {
        cvarManager->setAllowWritingInitOnlyVars(false);
    }
}

int CommandManagerImpl::execBufferedCommands(const std::uint32_t maxCommandsToExec)
{
    if (cmdBufferUsed == 0 || maxCommandsToExec == 0)
    {
        return 0;
    }

    int commandsExecuted = 0;
    const char * cmdBufferPtr = cmdBuffer;

    bool overflowed;
    char tempBuffer[MaxCommandArgStrLength];

    while (extractNextCommand(&cmdBufferPtr, tempBuffer, lengthOfArray(tempBuffer), &overflowed))
    {
        if (overflowed)
        {
            // Malformed command line that won't fit in our buffers.
            // Discard the rest of the command buffer and bail out.
            cmdBufferUsed = 0;
            cmdBuffer[0]  = '\0';
            errorF("Discarding rest of command buffer due to malformed command string...");
            break;
        }

        //
        // It is necessary to shift the buffer and remove every
        // executed command to ensure execInsert/execAppend will
        // work properly if called from within a command handler.
        // Doing this rather suboptimal memory shuffling allows
        // the user to define commands that push other buffered
        // commands, so the net gain pays it off.
        //
        const int charsConsumed = static_cast<int>(cmdBufferPtr - cmdBuffer);
        cmdBufferUsed -= charsConsumed;
        std::memmove(cmdBuffer, cmdBufferPtr, cmdBufferUsed);

        cmdBuffer[cmdBufferUsed] = '\0';
        cmdBufferPtr = cmdBuffer;

        // Call the handler:
        CommandArgs cmdArgs(tempBuffer);
        execTokenized(cmdArgs);
        ++commandsExecuted;

        // If we have already executed a ludicrous number of commands,
        // chances are there's a reentrant command in the buffer that
        // is adding itself again and again. This should catch it.
        if (commandsExecuted == MaxReentrantCommands)
        {
            cmdBufferUsed = 0;
            cmdBuffer[0]  = '\0';
            errorF("%i commands executed in sequence! Possible reentrant loop...", commandsExecuted);
            break;
        }

        // Reached our limit for this frame?
        if ((maxCommandsToExec != ExecAll) &&
            (static_cast<std::uint32_t>(commandsExecuted) == maxCommandsToExec))
        {
            break;
        }
    }

    // If we're at the end, make sure any trailing command separators and whitespace are discarded.
    if (*cmdBufferPtr == '\0')
    {
        cmdBufferUsed = 0;
        cmdBuffer[0]  = '\0';
    }

    return commandsExecuted;
}

bool CommandManagerImpl::hasBufferedCommands() const
{
    return cmdBufferUsed > 0;
}

void CommandManagerImpl::execTokenized(const CommandArgs & cmdArgs)
{
    // Validate the name length:
    const char * const cmdName = cmdArgs.getCommandName();
    if (lengthOfString(cmdName) >= MaxCommandNameLength)
    {
        errorF("Command name too long! Max command name length is %i characters.", MaxCommandNameLength);
        return;
    }

    // Find the command:
    auto cmd = registeredCommands.findByKey(cmdName);
    if (cmd == nullptr)
    {
        errorF("%s: Command not found.", cmdName);
        return;
    }

    // Check if this command is currently allowed to execute:
    if (disabledCmdFlags != 0)
    {
        if (disabledCmdFlags == DisableAll)
        {
            errorF("Command execution is globally disabled!");
            return;
        }
        if (cmd->getFlags() & disabledCmdFlags)
        {
            errorF("%s: Command is disabled!", cmdName);
            return;
        }
    }

    // Optional min/max arguments validation, if they are not negative (zero args is OK):
    if (cmd->getMinArgs() >= 0)
    {
        if (cmdArgs.getArgCount() < cmd->getMinArgs())
        {
            errorF("%s: Not enough arguments! Expected at least %i.", cmdName, cmd->getMinArgs());
            return;
        }
    }

    if (cmd->getMaxArgs() >= 0)
    {
        if (cmdArgs.getArgCount() > cmd->getMaxArgs())
        {
            errorF("%s: Too many arguments provided! Expected up to %i.", cmdName, cmd->getMaxArgs());
            return;
        }
    }

    // Arguments pre-validated, call command handler:
    cmd->onExecute(cmdArgs);
}

bool CommandManagerImpl::extractNextCommand(const char ** outStr, char * destBuf, const int destSizeInChars, bool * outOverflowed) const
{
    const char * str = *outStr;
    *outOverflowed   = false;

    // First, sanitize leading command separators and whitespace
    // that might have been left over from a previous pass.
    for (; *str != '\0'; ++str)
    {
        const int chr = *str;
        if (!isWhitespace(chr) && chr != CommandTextSeparator)
        {
            break;
        }
    }

    //
    // This simple parsing loop allows for commands mostly compatible
    // with Unix Shell implementations. A couple examples:
    //
    // cmd_1 "hello world" \
    //       "goodbye galaxy"
    //
    // ^ The backslash allows a multi-line command/arg-list.
    //
    // Multiple commands can also be placed on the same line if separated
    // by a CommandTextSeparator (by default a ';'), example:
    //
    // cmd_1 hello; cmd_2 goodbye; cmd_3 "commands made easy"
    //
    // Quotes will keep whitespace-separated strings together as a single argument,
    // ignoring occurrences of command separators inside the quoted blocks.
    //

    int  charsCopied = 0;
    int  quoteCount  = 0;
    bool quoted      = false;
    bool singleQuote = false;
    bool backslash   = false;
    bool done        = false;

    for (; !done && *str != '\0' && charsCopied < destSizeInChars; ++str)
    {
        const int chr = *str;
        if (chr == '\r')
        {
            // Silently ignore Windows Carriage Returns.
            // We only care about newlines.
            continue;
        }
        else if (chr == '\\')
        {
            // A backslash allows a multi-line command.
            backslash = true;
            continue;
        }
        else if (chr == '\n')
        {
            // Ignore the newline if preceded by a backslash or between quotes.
            done = !backslash && !quoted;
            backslash = false;
        }
        else if (chr == '"')
        {
            // Remember if we are inside a quoted block.
            // We have to ignore CommandTextSeparators inside them.
            ++quoteCount;
            quoted = quoteCount & 1;
        }
        else if (chr == '\'')
        {
            // Single quotes can start a quoted block or appear inside a double-quotes block.
            if (!quoted)
            {
                ++quoteCount;
                quoted = quoteCount & 1;
                singleQuote = true;
            }
            else if (singleQuote)
            {
                ++quoteCount;
                quoted = quoteCount & 1;
                singleQuote = false;
            }
        }
        else if (chr == CommandTextSeparator)
        {
            // Default command separator breaks a command
            // string if not inside a quoted block.
            done = !quoted;
        }
        #if CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION
        else if (chr == '$' && *(str + 1) == '(')
        {
            if (!expandCVar(&str, &charsCopied, destBuf, destSizeInChars, 1))
            {
                // Skip the rest of the broken command.
                for (; *str != '\0' && *str != '\n' && *str != CommandTextSeparator; ++str) { }
                *outOverflowed = true; // This makes the command string be discarded.
                done = true;
            }
            continue;
        }
        #endif // CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION

        if (backslash && !(chr == ' ' || chr == '\t'))
        {
            // This prevents errors from lost backslashes in the middle of a string,
            // which are probably leftovers from collapsing a line. Just ignore it.
            backslash = false;
        }

        if (!done)
        {
            destBuf[charsCopied++] = chr;
        }
    }

    if (charsCopied == destSizeInChars)
    {
        --charsCopied; // Truncated.
        *outOverflowed = true;
        errorF("Command string too long! Can't parse all arguments from it...");
    }

    destBuf[charsCopied] = '\0';
    *outStr = str;

    return charsCopied > 0;
}

#if CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION
bool CommandManagerImpl::expandCVar(const char ** outStr, int * outCharsCopied, char * destBuf,
                                    const int destSizeInChars, const int recursionDepth) const
{
    if (cvarManager == nullptr)
    {
        return errorF("No CVarManager set. Unable to perform CVar argument expansion.");
    }

    // Max recursion depth for reentrant CVar expansions.
    // If the maximum is reached, the whole substitution will fail.
    constexpr int MaxRecursionDepth = 15;

    const char * str   = *outStr;
    int  parenthesis   = 0;
    int  varNameLength = 0;
    char varName[MaxCommandArgStrLength];

    // Function should be entered while pointing to the '$'
    CFG_ASSERT(*str == '$');

    for (++str; *str != '\0' && *str != '\n' && *str != CommandTextSeparator; ++str)
    {
        if (*str == '(')
        {
            ++parenthesis;
        }
        else if (*str == ')')
        {
            --parenthesis;
            break;
        }
        else if (*str == '$' && *(str + 1) == '(') // Reentrant expansion
        {
            if (recursionDepth == MaxRecursionDepth)
            {
                return errorF("Too many reentrant CVar argument expansions!");
            }
            if (!expandCVar(&str, &varNameLength, varName, MaxCommandArgStrLength, recursionDepth + 1))
            {
                return false;
            }
        }
        else // Part of the var name
        {
            if (!isWhitespace(*str))
            {
                if (varNameLength == MaxCommandArgStrLength)
                {
                    return errorF("Buffer overflow in CVar name expansion!");
                }
                varName[varNameLength++] = *str;
            }
        }
    }

    if (parenthesis != 0)
    {
        return errorF("Unbalanced opening or closing parenthesis in CVar argument expansion!");
    }
    if (varNameLength == 0)
    {
        return errorF("Missing CVar name in argument expansion!");
    }

    varName[varNameLength] = '\0';
    if (!cvarManager->isValidCVarName(varName))
    {
        return errorF("Invalid CVar name '%s' in argument expansion!", varName);
    }

    const CVar * const cvar = cvarManager->findCVar(varName);
    if (cvar == nullptr)
    {
        return errorF("Trying to expand undefined CVar '$(%s)'.", varName);
    }

    const int bufferOffset     = *outCharsCopied;
    const std::string varValue = cvar->getStringValue();

    *outCharsCopied += copyString(destBuf + bufferOffset, destSizeInChars - bufferOffset, varValue.c_str());
    *outStr = str;

    return true;
}
#endif // CFG_COMMAND_PERFORM_CVAR_SUBSTITUTION

// ========================================================
// Virtual destructors anchored to this file:
// ========================================================

Command::~Command()
{ }

CommandManager::~CommandManager()
{ }

CommandHandlerMemFunc::BaseCallableHolder::~BaseCallableHolder()
{ }

// ================================================================================================
//
//                                 Interactive Terminal/Console
//
// ================================================================================================

// ========================================================
// color::canColorPrint():
// ========================================================

namespace color
{
bool canColorPrint() noexcept
{
    #if CFG_USE_ANSI_COLOR_CODES
    return CFG_ISATTY(CFG_STDOUT_FILENO) && CFG_ISATTY(CFG_STDERR_FILENO);
    #else // !CFG_USE_ANSI_COLOR_CODES
    return false;
    #endif // CFG_USE_ANSI_COLOR_CODES
}
} // namespace color {}

// ========================================================
// SpecialKeys::toString():
// ========================================================

std::string SpecialKeys::toString(const int key)
{
    std::string str;
    switch (key)
    {
    case Return     : { str = "Return";     break; }
    case Tab        : { str = "Tab";        break; }
    case Backspace  : { str = "Backspace";  break; }
    case Delete     : { str = "Delete";     break; }
    case UpArrow    : { str = "UpArrow";    break; }
    case DownArrow  : { str = "DownArrow";  break; }
    case RightArrow : { str = "RightArrow"; break; }
    case LeftArrow  : { str = "LeftArrow";  break; }
    case Escape     : { str = "Escape";     break; }
    case Control    : { str = "Control";    break; }
    default         : { str.push_back(static_cast<char>(key)); break; }
    } // switch (key)
    return str;
}

// ========================================================
// BuiltInCmd handlers:
// ========================================================

static void cmdExit(SimpleCommandTerminal & term)      { term.setExit();             }
static void cmdClear(SimpleCommandTerminal & term)     { term.clear();               }
static void cmdHistView(SimpleCommandTerminal & term)  { term.printCommandHistory(); }
static void cmdHistClear(SimpleCommandTerminal & term) { term.clearCommandHistory(); }
static void cmdHistSave(SimpleCommandTerminal & term)  { term.saveCommandHistory();  }
static void cmdHistLoad(SimpleCommandTerminal & term)  { term.loadCommandHistory();  }

// ========================================================
// List of built-in commands:
// ========================================================

const SimpleCommandTerminal::BuiltInCmd SimpleCommandTerminal::builtInCmds[]
{
    { &cmdExit,      "exit",      "Exits the interactive terminal mode." },
    { &cmdClear,     "clear",     "Clears the terminal screen."          },
    { &cmdHistView,  "histView",  "Prints the current command history."  },
    { &cmdHistClear, "histClear", "Erases the current command history."  },
    { &cmdHistSave,  "histSave",  "Saves the current command history to \"" CFG_COMMAND_HIST_FILE "\"." },
    { &cmdHistLoad,  "histLoad",  "Load previous command history from \""   CFG_COMMAND_HIST_FILE "\"." }
};

// ========================================================
// SimpleCommandTerminal implementation:
// ========================================================

SimpleCommandTerminal::SimpleCommandTerminal(CommandManager * cmdMgr,
                                             CVarManager    * cvarMgr,
                                             const char     * newlineMark)
    : lineBufferUsed(0)
    , lineBufferInsertionPos(0)
    , currCmdInHistory(0)
    , indexCmdHistory(0)
    , lineHasMarker(false)
    , exitFlag(false)
    , listAllCmdsOnTab(false)
    , firstAutoCompletionTry(true)
    , oldLineBufferUsed(0)
    , nextDisplayedCompletionMatch(0)
    , partialStrLength(0)
    , completionMatchCount(0)
    , cmdManager(cmdMgr)
    , cvarManager(cvarMgr)
    , cmdExecMode(CommandExecMode::Append)
    , newlineMarkerStr(newlineMark)
{
    clearArray(completionMatches);
    clearArray(lineBuffer);
}

SimpleCommandTerminal::~SimpleCommandTerminal()
{
    // No cleanup needed.
    // Here to anchor the vtable to this file.
}

CommandManager * SimpleCommandTerminal::getCommandManager() const noexcept
{
    return cmdManager;
}

CommandExecMode SimpleCommandTerminal::getCommandExecMode() const noexcept
{
    return cmdExecMode;
}

CVarManager * SimpleCommandTerminal::getCVarManager() const noexcept
{
    return cvarManager;
}

void SimpleCommandTerminal::setCommandManager(CommandManager * cmdMgr) noexcept
{
    cmdManager = cmdMgr;
}

void SimpleCommandTerminal::setCommandExecMode(const CommandExecMode newMode) noexcept
{
    cmdExecMode = newMode;
}

void SimpleCommandTerminal::setCVarManager(CVarManager * cvarMgr) noexcept
{
    cvarManager = cvarMgr;
}

void SimpleCommandTerminal::setTextColor(const char * const ansiColorCode) noexcept
{
    print(ansiColorCode);
}

void SimpleCommandTerminal::restoreTextColor() noexcept
{
    print(color::restore());
}

void SimpleCommandTerminal::printF(const char * fmt, ...)
{
    CFG_ASSERT(fmt != nullptr);

    va_list vaList;
    char tempStr[LineBufferMaxSize];

    va_start(vaList, fmt);
    const int result = std::vsnprintf(tempStr, lengthOfArray(tempStr), fmt, vaList);
    va_end(vaList);

    if (result > 0)
    {
        print(tempStr);
    }
}

void SimpleCommandTerminal::clear()
{
    // If a child class overrides clear(), this should still
    // be called at the end to insert a new line marker in
    // the just cleared terminal buffer.

    print(newlineMarkerStr.c_str());

    lineBufferUsed               = 0;
    lineBufferInsertionPos       = 0;
    lineHasMarker                = true;
    listAllCmdsOnTab             = false;
    firstAutoCompletionTry       = true;
    oldLineBufferUsed            = 0;
    nextDisplayedCompletionMatch = 0;
    partialStrLength             = 0;
    completionMatchCount         = 0;

    clearArray(completionMatches);
    clearArray(lineBuffer);

    for (auto & s : argStringMatches)
    {
        s.clear();
    }
}

void SimpleCommandTerminal::update()
{
    // Might not have the newline marker yet if the CommandManager was still processing commands.
    if (!lineHasMarker && !exitFlag)
    {
        print(newlineMarkerStr.c_str());
        lineHasMarker = true;
    }
}

bool SimpleCommandTerminal::exit() const
{
    return exitFlag;
}

void SimpleCommandTerminal::cancelExit()
{
    exitFlag = false;
}

void SimpleCommandTerminal::setExit()
{
    exitFlag = true;
    onExit();
}

void SimpleCommandTerminal::onExit()
{
    // A no-op by default.
}

void SimpleCommandTerminal::onSetClipboardString(const char * /* str */)
{
    // A no-op by default.
}

const char * SimpleCommandTerminal::onGetClipboardString()
{
    return nullptr; // A no-op by default.
}

bool SimpleCommandTerminal::handleKeyInput(const int key, const char chr)
{
    // Reset some [TAB] auto-completion states:
    if (key != SpecialKeys::Tab)
    {
        listAllCmdsOnTab             = false;
        firstAutoCompletionTry       = true;
        nextDisplayedCompletionMatch = 0;
        oldLineBufferUsed            = 0;
    }

    switch (key)
    {
    case SpecialKeys::Return     : return finishCommand();
    case SpecialKeys::Tab        : return tabCompletion();
    case SpecialKeys::Backspace  : return popChar();
    case SpecialKeys::Delete     : return delChar();
    case SpecialKeys::UpArrow    : return nextCmdFromHistory();
    case SpecialKeys::DownArrow  : return prevCmdFromHistory();
    case SpecialKeys::RightArrow : return navigateTextRight();
    case SpecialKeys::LeftArrow  : return navigateTextLeft();
    case SpecialKeys::Escape     : return discardInput();
    case SpecialKeys::Control    : return handleCtrlKey(chr);
    default                      : return insertChar(chr);
    } // switch (key)
}

int SimpleCommandTerminal::getCommandHistorySize() const noexcept
{
    return indexCmdHistory;
}

const char * SimpleCommandTerminal::getCommandFromHistory(const int index) const
{
    CFG_ASSERT(index >= 0 && index < getCommandHistorySize());
    return cmdHistory[index].c_str();
}

void SimpleCommandTerminal::clearCommandHistory()
{
    for (int i = 0; i < CmdHistoryMaxSize; ++i)
    {
        cmdHistory[i].clear();
    }

    currCmdInHistory = 0;
    indexCmdHistory  = 0;
}

void SimpleCommandTerminal::printCommandHistory()
{
    printLn("----- Command History -----");

    const int histSize = getCommandHistorySize();
    for (int i = 0; i < histSize; ++i)
    {
        printF("[%02i]: %s\n", i, getCommandFromHistory(i));
    }
}

bool SimpleCommandTerminal::saveCommandHistory()
{
    FileIOCallbacks * io = getFileIOCallbacks();

    FileHandle fileOut;
    if (!io->open(&fileOut, CFG_COMMAND_HIST_FILE, FileOpenMode::Write))
    {
        return false;
    }

    const int histSize = getCommandHistorySize();
    for (int i = 0; i < histSize; ++i)
    {
        io->writeFormat(fileOut, "%s\n", getCommandFromHistory(i));
    }

    printF("Command history saved to \"%s\".\n", CFG_COMMAND_HIST_FILE);
    io->close(fileOut);
    return true;
}

bool SimpleCommandTerminal::loadCommandHistory()
{
    FileIOCallbacks * io = getFileIOCallbacks();

    FileHandle fileIn;
    if (!io->open(&fileIn, CFG_COMMAND_HIST_FILE, FileOpenMode::Read))
    {
        return false;
    }

    clearCommandHistory(); // Current history is lost, if any.

    // Copy the new history:
    char line[LineBufferMaxSize];
    while (io->readLine(fileIn, line, lengthOfArray(line)))
    {
        addCmdToHistory(rightTrimString(line)); // RTrim to get rid of the newline.
    }

    printF("Command history restored from \"%s\".\n", CFG_COMMAND_HIST_FILE);
    io->close(fileIn);
    return true;
}

int SimpleCommandTerminal::getBuiltInCommandsCount() const noexcept
{
    return lengthOfArray(builtInCmds);
}

const SimpleCommandTerminal::BuiltInCmd * SimpleCommandTerminal::getBuiltInCommand(const int index) const
{
    CFG_ASSERT(index >= 0 && index < getBuiltInCommandsCount());
    return &builtInCmds[index];
}

const SimpleCommandTerminal::BuiltInCmd * SimpleCommandTerminal::getBuiltInCommand(const char * const name) const
{
    if (name == nullptr || *name == '\0')
    {
        return nullptr;
    }

    const int builtInCount = getBuiltInCommandsCount();
    for (int i = 0; i < builtInCount; ++i)
    {
        if (cmdCmpNames(name, builtInCmds[i].name) == 0)
        {
            return &builtInCmds[i];
        }
    }

    return nullptr;
}

void SimpleCommandTerminal::setLineBuffer(const char * str)
{
    if (str == nullptr)
    {
        return;
    }

    clearVisibleEditLine();
    if (*str != '\0')
    {
        print(str);
        lineBufferUsed = copyString(lineBuffer, lengthOfArray(lineBuffer), str);
        lineBufferInsertionPos = lineBufferUsed;
    }
    else
    {
        clearLineInputBuffer();
    }
}

const char * SimpleCommandTerminal::getLineBuffer() const
{
    return lineBuffer;
}

bool SimpleCommandTerminal::isLineBufferEmpty() const
{
    return lineBufferUsed == 0;
}

void SimpleCommandTerminal::newLineNoMarker()
{
    print("\n");
    lineHasMarker = false;
}

void SimpleCommandTerminal::newLineWithMarker()
{
    printF("\n%s", newlineMarkerStr.c_str());
    lineHasMarker = true;
}

void SimpleCommandTerminal::clearVisibleEditLine()
{
    // Fill the line with blanks to clear it out.
    char blankLine[LineBufferMaxSize];
    const int charsToFill = std::min(static_cast<int>(lineBufferUsed + newlineMarkerStr.length()), lengthOfArray(blankLine));

    std::memset(blankLine, ' ', charsToFill);
    blankLine[charsToFill] = '\0';

    printF("\r%s\r%s", blankLine, newlineMarkerStr.c_str());
    lineHasMarker = true;
}

void SimpleCommandTerminal::clearLineInputBuffer()
{
    lineBufferUsed         = 0;
    lineBufferInsertionPos = 0;
    lineBuffer[0]          = '\0';
}

bool SimpleCommandTerminal::finishCommand()
{
    // Break this line but don't put the new line marker yet,
    // let the command be processed first.
    newLineNoMarker();

    // Add to the history and run the command:
    if (!isLineBufferEmpty())
    {
        // But ignore blank lines/empty command.
        if (*lineBuffer != '\0' && *lineBuffer != '\n' && *lineBuffer != '\r')
        {
            addCmdToHistory(lineBuffer);
            execCmdLine(lineBuffer);
        }
        clearLineInputBuffer();
    }

    bool canAddMarker;
    if (cmdManager == nullptr)
    {
        canAddMarker = true;
    }
    else if (cmdManager->hasBufferedCommands())
    {
        canAddMarker = false;
    }
    else
    {
        canAddMarker = true;
    }

    if (canAddMarker && !lineHasMarker && !exitFlag)
    {
        print(newlineMarkerStr.c_str());
        lineHasMarker = true;
    }

    return true; // Input [RETURN] was handled.
}

void SimpleCommandTerminal::execCmdLine(const char * cmd)
{
    char cmdName[LineBufferMaxSize];
    const char * cmdPtr = cmd;

    // Skip leading whitespace:
    for (; *cmdPtr != '\0' && isWhitespace(*cmdPtr); ++cmdPtr) { }
    const char * firstNonWhite = cmdPtr;

    // Copy until the first whitespace to get the command name:
    char * p = cmdName;
    for (; *cmdPtr != '\0' && !isWhitespace(*cmdPtr); ++p, ++cmdPtr)
    {
        *p = *cmdPtr;
    }
    *p = '\0';

    if (*cmdName == '\0')
    {
        return; // Just whitespace.
    }

    //
    // Check if not setting a CVar with the shortcut notation:
    // "varName value"
    //
    // Or if printing the variable value by referencing it.
    //
    CVar * cvar;
    if ((cvarManager != nullptr) && (cvar = cvarManager->findCVar(cmdName)))
    {
        CommandArgs cmdArgs(cmd);
        if (cmdArgs.getArgCount() < 1)
        {
            printF("%s is: \"%s\"  |  default: \"%s\"\n",
                   cvar->getNameCString(),
                   cvar->getStringValue().c_str(),
                   cvar->getDefaultValueString().c_str());
        }
        else
        {
            if (cmdArgs.getArgCount() > 1)
            {
                setTextColor(color::yellow());
                printLn("CVar update takes one argument. Ignoring extraneous ones...");
                restoreTextColor();
            }
            if (!cvar->setStringValue(cmdArgs[0]))
            {
                setTextColor(color::yellow());
                printF("Cannot set %s to \"%s\"!\n", cmdName, cmdArgs[0]);
                restoreTextColor();
            }
        }
        return;
    }

    //
    // Check the built-in commands second:
    //
    if (const BuiltInCmd * const builtIn = getBuiltInCommand(cmdName))
    {
        builtIn->handler(*this);
        return;
    }

    //
    // Try the user-defined commands last:
    //
    if ((cmdManager != nullptr) && cmdManager->findCommand(cmdName))
    {
        cmdManager->execute(cmdExecMode, firstNonWhite);
        return;
    }

    printF("%s: Command not found.\n", cmdName);
}

void SimpleCommandTerminal::addCmdToHistory(const char * cmd)
{
    if (indexCmdHistory < CmdHistoryMaxSize) // Insert at front:
    {
        cmdHistory[indexCmdHistory++] = cmd;
    }
    else // Full, remove oldest:
    {
        CFG_ASSERT(indexCmdHistory == CmdHistoryMaxSize);

        // Pop tail and shift everyone:
        for (int i = 0; i < indexCmdHistory - 1; ++i)
        {
            cmdHistory[i] = cmdHistory[i + 1];
        }

        // Insert new at front:
        cmdHistory[indexCmdHistory - 1] = cmd;
    }

    currCmdInHistory = indexCmdHistory - 1;
}

bool SimpleCommandTerminal::nextCmdFromHistory()
{
    CFG_ASSERT(currCmdInHistory >= 0 && currCmdInHistory < lengthOfArray(cmdHistory));
    const char * cmd = cmdHistory[currCmdInHistory].c_str();

    if (currCmdInHistory > 0)
    {
        --currCmdInHistory;
        if (std::strcmp(cmd, lineBuffer) == 0 && currCmdInHistory >= 0)
        {
            cmd = cmdHistory[currCmdInHistory].c_str();
        }
    }

    setLineBuffer(cmd);
    return true;
}

bool SimpleCommandTerminal::prevCmdFromHistory()
{
    const char * cmd;
    if (currCmdInHistory < (indexCmdHistory - 1))
    {
        ++currCmdInHistory;
        CFG_ASSERT(currCmdInHistory >= 0 && currCmdInHistory < lengthOfArray(cmdHistory));
        cmd = cmdHistory[currCmdInHistory].c_str();

        // Need this check if going up and down on the same two commands.
        if (std::strcmp(cmd, lineBuffer) == 0 && currCmdInHistory < indexCmdHistory)
        {
            ++currCmdInHistory;
            cmd = cmdHistory[currCmdInHistory].c_str();
        }
    }
    else
    {
        cmd = "";
    }

    setLineBuffer(cmd);
    return true;
}

bool SimpleCommandTerminal::navigateTextLeft()
{
    if (lineBufferInsertionPos <= 0)
    {
        return true; // Return whether the input was handled or not.
                     // In this case, always true.
    }

    lineBufferInsertionPos--;
    redrawInputLine();
    return true;
}

bool SimpleCommandTerminal::navigateTextRight()
{
    if (lineBufferInsertionPos >= lineBufferUsed)
    {
        return true; // Same as above.
    }

    lineBufferInsertionPos++;
    redrawInputLine();
    return true;
}

void SimpleCommandTerminal::redrawInputLine()
{
    // Workaround to position the cursor without
    // a gotoxy()-like function: redraw the whole line.
    printF("\r%s%.*s", newlineMarkerStr.c_str(), lineBufferInsertionPos, lineBuffer);
}

bool SimpleCommandTerminal::handleCtrlKey(const char chr)
{
    switch (chr)
    {
    // Copy input line to clipboard: PC=CTRL+c, Mac=CMD+c
    case 'c' :
        onSetClipboardString(lineBuffer);
        return true;

    // Paste from clipboard: PC=CTRL+v, Mac=CMD+v
    case 'v' :
        for (const char * str = onGetClipboardString();
             str != nullptr && *str != '\0'; ++str)
        {
            // Using this loop to allow inserting at the current cursor pos.
            insertChar(*str);
        }
        return true;

    // [CTRL] + l clears the screen
    case 'l' :
        clear();
        return true;

    // Unix-style command history: [CTRL] + n = previous command
    case 'n' :
        return prevCmdFromHistory();

    // Unix-style command history: [CTRL] + p = next command
    case 'p' :
        return nextCmdFromHistory();

    default :
        return false;
    } // switch (chr)
}

bool SimpleCommandTerminal::hasFullNameInLineBuffer() const
{
    bool foundNonWhiteChar = false;
    for (int i = 0; i < lineBufferUsed; ++i)
    {
        if (lineBuffer[i] != ' ')
        {
            foundNonWhiteChar = true;
        }
        else
        {
            if (!foundNonWhiteChar)
            {
                continue;
            }
            return true;
        }
    }
    return false;
}

void SimpleCommandTerminal::listAllCommands()
{
    if (cmdManager == nullptr)
    {
        return;
    }

    if (!listAllCmdsOnTab)
    {
        printF("\rPress [%s] again to list commands...\n",
               SpecialKeys::toString(SpecialKeys::Tab).c_str());
        listAllCmdsOnTab = true;
        return;
    }

    //
    // List all available commands (or up to MaxCompletionMatches):
    //

    struct CmdList
    {
        int count;
        const Command * cmds[MaxCompletionMatches];
    };
    CmdList cmdList{};

    cmdManager->enumerateAllCommands(
            [](Command * cmd, void * userContext)
            {
                auto list = static_cast<CmdList *>(userContext);
                if (list->count == SimpleCommandTerminal::MaxCompletionMatches)
                {
                    return false; // Stop the enumeration.
                }
                list->cmds[list->count++] = cmd;
                return true;
            },
            &cmdList);

    std::sort(cmdList.cmds, cmdList.cmds + cmdList.count,
              [](const Command * const a, const Command * const b)
              {
                  return cmdCmpNames(a->getNameCString(), b->getNameCString()) < 0;
              });

    int cmdsWrittenInALine = 0;
    const int builtInCount = getBuiltInCommandsCount();

    // List the build-ins first in alternate color:
    setTextColor(color::cyan());
    for (int c = 0; c < builtInCount; ++c)
    {
        const BuiltInCmd * const cmd = getBuiltInCommand(c);
        printF("%-*s", MaxCommandNameLength, cmd->name);

        if (++cmdsWrittenInALine >= MaxCmdMatchesPerLine && c != (builtInCount - 1))
        {
            cmdsWrittenInALine = 0;
            print("\n");
        }
    }

    // Now the rest in normal color:
    restoreTextColor();
    for (int c = 0; c < cmdList.count; ++c)
    {
        const Command * const cmd = cmdList.cmds[c];
        printF("%-*s", MaxCommandNameLength, cmd->getNameCString());

        if (++cmdsWrittenInALine >= MaxCmdMatchesPerLine && c != (cmdList.count - 1))
        {
            cmdsWrittenInALine = 0;
            print("\n");
        }
    }

    const int totalCmds = cmdManager->getRegisteredCommandsCount();
    if (cmdList.count < totalCmds)
    {
        setTextColor(color::cyan());
        printF("\n+%i commands...", (totalCmds - cmdList.count));
        restoreTextColor();
    }

    newLineWithMarker();
    listAllCmdsOnTab = false;
}

void SimpleCommandTerminal::listMatches(const char * const partialStr, const char ** matches, const int matchesFound,
                                        const int maxMatches, const int maxPerLine, const int spacing, const bool coloredMatch)
{
    int writtenInALine   = 0;
    const int partialLen = lengthOfString(partialStr);

    for (int i = 0; i < matchesFound; ++i)
    {
        if (i != maxMatches)
        {
            CFG_ASSERT(matches[i] != nullptr);

            if (coloredMatch)
            {
                setTextColor(color::cyan());
                printF("%.*s", partialLen, matches[i]);
                restoreTextColor();
            }
            else
            {
                printF("%.*s", partialLen, matches[i]);
            }

            if (maxPerLine > 1)
            {
                printF("%-*s", spacing - partialLen, matches[i] + partialLen); // A bunch per line, column align.
            }
            else
            {
                printF("%s", matches[i] + partialLen); // One per line.
            }
        }
        else
        {
            setTextColor(color::cyan());
            printF("+%i matches...", (matchesFound - maxMatches));
            restoreTextColor();
            break;
        }

        if (++writtenInALine >= maxPerLine)
        {
            if ((matchesFound - i) > 1)
            {
                print("\n");
            }
            writtenInALine = 0;
        }
    }
}

bool SimpleCommandTerminal::displayCompletionMatches(const char * const partialString, const int maxPerLine,
                                                     const bool whitespaceAfter1Match, const bool allowCyclingValues,
                                                     const FindMatchesCallback & findMatchesCb)
{
    if (firstAutoCompletionTry) // Fist [TAB], list possible matches and save them for subsequent cycling:
    {
        int matchesFound = 0;
        findMatchesCb(*this, partialString, completionMatches, &matchesFound, MaxCompletionMatches);

        if (matchesFound <= 0)
        {
            return false; // No matches for auto-completion.
        }

        if (matchesFound == 1) // Exactly one:
        {
            const int partialLen    = lengthOfString(partialString);
            const char * completion = completionMatches[0] + partialLen;

            lineBufferUsed += copyString(lineBuffer + lineBufferUsed, lengthOfArray(lineBuffer) - lineBufferUsed, completion);

            if (whitespaceAfter1Match)
            {
                lineBuffer[lineBufferUsed++] = ' ';
                lineBuffer[lineBufferUsed]   = '\0';
                printF("%s ", completion);
            }
            else
            {
                printF("%s", completion);
            }

            lineBufferInsertionPos = lineBufferUsed;
        }
        else // There is more than one match, print a list:
        {
            const int maxMatches = MaxCompletionMatches;

            newLineNoMarker();
            listMatches(partialString, completionMatches, matchesFound, maxMatches,
                        maxPerLine, MaxCommandNameLength, allowCyclingValues);
            newLineWithMarker();
            print(lineBuffer);

            if (allowCyclingValues)
            {
                // Save for [TAB] cycling:
                firstAutoCompletionTry = false;
                completionMatchCount   = std::min(matchesFound, maxMatches);
                partialStrLength       = lengthOfString(partialString);
                oldLineBufferUsed      = lineBufferUsed;
            }
        }

        return true;
    }
    else // Cycling a list of possible matches after the first [TAB]:
    {
        const int partialLen  = std::min(partialStrLength, LineBufferMaxSize - 1);
        const char * matchStr = completionMatches[nextDisplayedCompletionMatch];

        clearVisibleEditLine();
        if ((oldLineBufferUsed - partialLen) > 0)
        {
            printF("%.*s", oldLineBufferUsed - partialLen, lineBuffer);
        }

        // Partial name in normal color:
        printF("%.*s", partialLen, matchStr);

        // Auto-completed guess in alternate color:
        setTextColor(color::cyan());
        print(matchStr + partialLen);
        restoreTextColor();

        lineBufferUsed  = oldLineBufferUsed;
        lineBufferUsed += copyString(lineBuffer + lineBufferUsed, lengthOfArray(lineBuffer) - lineBufferUsed, matchStr + partialLen);
        lineBufferInsertionPos = lineBufferUsed;

        // Cycle in case the user hits TAB again.
        nextDisplayedCompletionMatch = (nextDisplayedCompletionMatch + 1) % completionMatchCount;
        return true;
    }
}

bool SimpleCommandTerminal::tabCompletion()
{
    if (lineBufferInsertionPos != lineBufferUsed)
    {
        // Auto-completion not attempted if the cursor is not at the end of input line.
        return true;
    }

    if (isLineBufferEmpty())
    {
        listAllCommands(); // Only after 2 consecutive TABs.
        return true;
    }

    // A Command or CVar name is already completed, now we'll do argument auto-completion:
    if (hasFullNameInLineBuffer())
    {
        // Check for "$(var)" CVar substitution/expansion and try to complete the var name:
        if (const char * cvarExpansionPtr = std::strrchr(lineBuffer, '$'))
        {
            if (cvarManager != nullptr && *(++cvarExpansionPtr) == '(')
            {
                if (!std::strrchr(++cvarExpansionPtr, ')')) // If the "$(..." not closed yet, try completing:
                {
                    auto findMatchesCallback = [](SimpleCommandTerminal & term, const char * const partialName,
                                                  const char ** outMatches, int * outMatchesFound, const int maxMatches)
                    {
                        auto cvarMgr = term.getCVarManager();
                        *outMatchesFound = cvarMgr->findCVarsWithPartialName(partialName, outMatches, maxMatches);
                    };

                    const char * const partialCVarName = cvarExpansionPtr;
                    displayCompletionMatches(partialCVarName, MaxCVarMatchesPerLine, false, true, findMatchesCallback);
                    return true;
                }
            }
        }

        // Command argument or CVar value auto-completion:
        char cmdName[LineBufferMaxSize];
        const char * cmdPtr = lineBuffer;

        // Skip leading whitespace:
        for (; *cmdPtr != '\0' && isWhitespace(*cmdPtr); ++cmdPtr) { }

        // Copy until the first whitespace to get the command name:
        char * p = cmdName;
        for (; *cmdPtr != '\0' && !isWhitespace(*cmdPtr); ++p, ++cmdPtr)
        {
            *p = *cmdPtr;
        }
        *p = '\0';

        int  argCount = 0;
        bool quotes   = false;
        bool gotWhite = false;

        // Last partial argument is the completion target.
        // Quoted strings keep whitespace.
        const char * lastWsPtr = cmdPtr;
        for (; *cmdPtr != '\0'; ++cmdPtr)
        {
            if (!quotes && isWhitespace(*cmdPtr))
            {
                lastWsPtr = cmdPtr; // Save the last whitespace.
                gotWhite  = true;
            }
            else if (*cmdPtr == '"' || *cmdPtr == '\'')
            {
                quotes = !quotes;
            }
            else // Not whitespace, not quotes:
            {
                if (gotWhite)
                {
                    gotWhite = false;
                    ++argCount;
                }
            }
        }

        // Get to the start of the partial argument:
        for (; *lastWsPtr != '\0' && isWhitespace(*lastWsPtr); ++lastWsPtr) { }
        const char * const partialArgStr = lastWsPtr;

        //
        // See if we are completing the value of a CVar or arguments of a command:
        //
        const CVar    * cvar = nullptr;
        const Command * cmd  = nullptr;

        if ((cvarManager != nullptr) && (cvar = cvarManager->findCVar(cmdName)))
        {
            std::string * varValueStrings = argStringMatches;
            auto findMatchesCallback = [cvar, varValueStrings](SimpleCommandTerminal &, const char * const partialArg,
                                                               const char ** outMatches, int * outMatchesFound, const int maxMatches)
            {
                CFG_ASSERT(maxMatches <= MaxCompletionMatches);
                const int matchesFound = cvar->valueCompletion(partialArg, varValueStrings, maxMatches);

                const int count = std::min(matchesFound, maxMatches);
                for (int m = 0; m < count; ++m)
                {
                    outMatches[m] = varValueStrings[m].c_str();
                }
                *outMatchesFound = matchesFound;
            };

            const bool allowCyclingValues = (*partialArgStr == '\0' || cvar->getValueCompletionCallback() != nullptr);
            displayCompletionMatches(partialArgStr, MaxArgMatchesPerLine, false, allowCyclingValues, findMatchesCallback);
        }
        else if ((cmdManager != nullptr) && (cmd = cmdManager->findCommand(cmdName)))
        {
            std::string * argCompletionStrings = argStringMatches;
            auto findMatchesCallback = [cmd, argCompletionStrings](SimpleCommandTerminal &, const char * const partialArg,
                                                                   const char ** outMatches, int * outMatchesFound, const int maxMatches)
            {
                CFG_ASSERT(maxMatches <= MaxCompletionMatches);
                const int matchesFound = cmd->argumentCompletion(partialArg, argCompletionStrings, maxMatches);

                const int count = std::min(matchesFound, maxMatches);
                for (int m = 0; m < count; ++m)
                {
                    outMatches[m] = argCompletionStrings[m].c_str();
                }
                *outMatchesFound = matchesFound;
            };
            displayCompletionMatches(partialArgStr, MaxArgMatchesPerLine, true, true, findMatchesCallback);
        }
        // else no match found.
    }
    else // Not a full name typed yet, try Cmd/CVar name completion:
    {
        // Skip leading whitespace:
        const char * partialCmdName = lineBuffer;
        for (; *partialCmdName != '\0' && isWhitespace(*partialCmdName); ++partialCmdName) { }

        // Turns out it was an empty string.
        if (*partialCmdName == '\0')
        {
            return true;
        }

        //
        // Try the console built-ins first:
        //
        bool matchFound;
        {
            auto findMatchesCallback = [](SimpleCommandTerminal & term, const char * const partialName,
                                          const char ** outMatches, int * outMatchesFound, const int maxMatches)
            {
                int matchesFound = 0;
                const int partialLen = lengthOfString(partialName);
                for (int i = 0; i < term.getBuiltInCommandsCount(); ++i)
                {
                    if (cmdCmpNames(partialName, term.getBuiltInCommand(i)->name, partialLen) == 0)
                    {
                        if (matchesFound < maxMatches)
                        {
                            outMatches[matchesFound] = term.getBuiltInCommand(i)->name;
                        }
                        ++matchesFound;
                    }
                }
                *outMatchesFound = matchesFound;
            };
            matchFound = displayCompletionMatches(partialCmdName, MaxCmdMatchesPerLine, true, true, findMatchesCallback);
        }

        //
        // CVar names second, to check for setting the var value:
        //
        if (!matchFound && cvarManager != nullptr)
        {
            auto findMatchesCallback = [](SimpleCommandTerminal & term, const char * const partialName,
                                          const char ** outMatches, int * outMatchesFound, const int maxMatches)
            {
                auto cvarMgr = term.getCVarManager();
                *outMatchesFound = cvarMgr->findCVarsWithPartialName(partialName, outMatches, maxMatches);
            };
            matchFound = displayCompletionMatches(partialCmdName, MaxCVarMatchesPerLine, true, true, findMatchesCallback);
        }

        //
        // Lastly the user-defined commands:
        //
        if (!matchFound && cmdManager != nullptr)
        {
            auto findMatchesCallback = [](SimpleCommandTerminal & term, const char * const partialName,
                                          const char ** outMatches, int * outMatchesFound, const int maxMatches)
            {
                auto cmdMgr = term.getCommandManager();
                *outMatchesFound = cmdMgr->findCommandsWithPartialName(partialName, outMatches, maxMatches);
            };
            displayCompletionMatches(partialCmdName, MaxCmdMatchesPerLine, true, true, findMatchesCallback);
        }
    }

    return true;
}

bool SimpleCommandTerminal::discardInput()
{
    currCmdInHistory = indexCmdHistory - 1; // Reset command history traversal marker.
    setLineBuffer("");
    return true;
}

bool SimpleCommandTerminal::popChar()
{
    if (isLineBufferEmpty() || lineBufferInsertionPos <= 0)
    {
        return true; // Input is always handled by this call, so return true.
    }

    clearVisibleEditLine();

    if (lineBufferInsertionPos == lineBufferUsed) // Erasing last character only:
    {
        lineBufferUsed--;
        lineBufferInsertionPos--;

        lineBuffer[lineBufferUsed] = '\0';
        print(lineBuffer);
    }
    else // Erasing at arbitrary position:
    {
        lineBufferUsed--;
        lineBufferInsertionPos--;

        int i;
        for (i = lineBufferInsertionPos; i != lineBufferUsed; ++i)
        {
            lineBuffer[i] = lineBuffer[i + 1];
        }
        lineBuffer[i] = '\0';

        // Reposition the cursor:
        print(lineBuffer);
        redrawInputLine();
    }

    return true;
}

bool SimpleCommandTerminal::delChar()
{
    if (isLineBufferEmpty() || lineBufferInsertionPos == lineBufferUsed)
    {
        return true; // Input is always handled by this call, so return true.
    }

    clearVisibleEditLine();
    lineBufferUsed--;

    int i;
    for (i = lineBufferInsertionPos; i != lineBufferUsed; ++i)
    {
        lineBuffer[i] = lineBuffer[i + 1];
    }
    lineBuffer[i] = '\0';

    // Reposition the cursor:
    print(lineBuffer);
    redrawInputLine();
    return true;
}

bool SimpleCommandTerminal::insertChar(const char chr)
{
    if (!std::isprint(chr) || lineBufferUsed >= (LineBufferMaxSize - 1))
    {
        return false; // Not printable, don't consume the input event.
    }

    if (lineBufferInsertionPos == lineBufferUsed)
    {
        // Inserting at the end, usual case.
        lineBuffer[lineBufferUsed++] = chr;
        lineBuffer[lineBufferUsed]   = '\0';
        lineBufferInsertionPos++;

        // Print the char just pushed:
        printF("%c", chr);
    }
    else // Inserting at any other position:
    {
        int i;
        for (i = lineBufferUsed; i > lineBufferInsertionPos; --i)
        {
            lineBuffer[i] = lineBuffer[i - 1];
        }

        lineBuffer[i] = chr;
        lineBuffer[++lineBufferUsed] = '\0';
        lineBufferInsertionPos++;

        // Position the cursor:
        clearVisibleEditLine();
        print(lineBuffer);
        redrawInputLine();
    }

    return true;
}

// ========================================================
// class UnixTerminal:
// ========================================================

#ifdef CFG_BUILD_UNIX_TERMINAL

//
// The input thread owns stdin
// The main thread owns stdout
//
class UnixTerminal final
    : public NativeTerminal
{
public:

     UnixTerminal();
    ~UnixTerminal();

    bool isTTY()    const override;
    bool hasInput() const override;
    int  getInput() override;

    void print(const char * text)   override;
    void printLn(const char * text) override;

    void clear()  override;
    void onExit() override;

    // Since clipboard handling requires system-specific code this
    // class just implements a simple application-side replacement
    // by keeping a local string. So it will work inside the terminal
    // but cannot be shared with any other applications.
    void onSetClipboardString(const char * str) override;
    const char * onGetClipboardString() override;

private:

    static void sysCls();
    static int  sysWaitChar();
    static void inputThreadFunction(UnixTerminal * term);
    void printWelcomeMessage();

private:

    // Terminal IO with the TERMIOS library.
    struct termios oldTermAttr;
    struct termios newTermAttr;

    // True if stdin & stdout are NOT redirected and we can run interactive mode.
    volatile bool isATerminal;

    // Signals the input thread to return. No need for an atomic,
    // there's no harm if the input thread sees an inconsistent
    // state for a frame, it will get the message eventually.
    volatile bool quitInputThread;

    // Thread that listens to the user input and writes to inputBuffer[].
    std::thread inputThread;

    // Next index to write in the input buffer/stack.
    std::atomic<int> inputBufferInsertionPos;

    // Line input buffer for our async terminal:
    static constexpr int InputBufferSize = 2048;
    std::int32_t inputBuffer[InputBufferSize];

    // Local clipboard string, to avoid OS-specific code.
    // Unfortunately cannot be shared with external applications.
    std::string clipboardString;
};

// ========================================================
// UnixTerminal implementation:
// ========================================================

UnixTerminal::UnixTerminal()
    : isATerminal(false)
    , quitInputThread(true)
{
    if (!CFG_ISATTY(CFG_STDIN_FILENO) || !CFG_ISATTY(CFG_STDOUT_FILENO))
    {
        errorF("STDIN/STDOUT is not a TTY! UnixTerminal refuses to run.");
        return;
    }

    //
    // Set termios attributes for standard input:
    //
    if (tcgetattr(CFG_STDIN_FILENO, &oldTermAttr) != 0)
    {
        errorF("Failed to get current terminal settings!");
        return;
    }

    newTermAttr = oldTermAttr;      // Make new settings same as old settings
    newTermAttr.c_lflag &= ~ICANON; // Disable buffered IO
    newTermAttr.c_lflag &= ~ECHO;   // Disable input echo
    newTermAttr.c_lflag &= ~ISIG;   // Disable CTRL+c interrupt signals, so we can use it for copy/paste
    newTermAttr.c_cc[VMIN] = 1;     // Minimum input required = 1 char

    if (tcsetattr(CFG_STDIN_FILENO, TCSANOW, &newTermAttr) != 0)
    {
        errorF("Failed to set new terminal settings!");
        return;
    }

    isATerminal             = true;
    quitInputThread         = false;
    inputBufferInsertionPos = 0;
    clearArray(inputBuffer);

    std::cout.sync_with_stdio(false);
    std::cout << std::unitbuf; // Unbuffered.

    printWelcomeMessage();

    // Start the input listener thread:
    inputThread = std::thread(inputThreadFunction, this);
}

UnixTerminal::~UnixTerminal()
{
    // It is very important to restore the original attributes
    // otherwise the OS might not do it when the application exits.
    // But only restore it if we ever had a chance to initialize.
    if (isATerminal)
    {
        tcsetattr(CFG_STDIN_FILENO, TCSANOW, &oldTermAttr);
    }

    // Wait for the input thread to return...
    quitInputThread = true;
    if (inputThread.joinable())
    {
        inputThread.join();
    }
}

void UnixTerminal::inputThreadFunction(UnixTerminal * term)
{
    CFG_ASSERT(term != nullptr);

    // Keep checking for input until the terminal is shutdown.
    while (!term->quitInputThread && term->isATerminal)
    {
        if (term->inputBufferInsertionPos < UnixTerminal::InputBufferSize)
        {
            term->inputBuffer[term->inputBufferInsertionPos++] = sysWaitChar();
        }
    }
}

bool UnixTerminal::isTTY() const
{
    return isATerminal;
}

bool UnixTerminal::hasInput() const
{
    if (!isATerminal)
    {
        // Never return input if not initialized properly.
        return false;
    }
    return inputBufferInsertionPos > 0;
}

int UnixTerminal::getInput()
{
    if (!isATerminal || inputBufferInsertionPos <= 0)
    {
        // Null input if not initialized properly
        // or if nothing in the buffer.
        return 0;
    }
    return inputBuffer[--inputBufferInsertionPos];
}

void UnixTerminal::printWelcomeMessage()
{
    if (!isATerminal)
    {
        return;
    }

    sysCls();

    const std::time_t currTime = std::time(nullptr);
    std::string timeStr = std::ctime(&currTime);
    timeStr.pop_back(); // Pop the default newline added by ctime.

    printF("+----------%s Unix Terminal %s----------+\n"
           "|   Session started: %s   |\n"
           "|     %s      |\n"
           "+-----------------------------------+\n",
           color::cyan(), color::restore(),
           ttyname(CFG_STDIN_FILENO), timeStr.c_str());

    newLineWithMarker();
}

void UnixTerminal::print(const char * text)
{
    // We can print even if redirected to a file.
    if (text != nullptr && *text != '\0')
    {
        std::cout << text;
    }
}

void UnixTerminal::printLn(const char * text)
{
    // We can print even if redirected to a file.
    // printLn() can take an empty string to just output the newline.
    if (text != nullptr)
    {
        std::cout << text << std::endl;
    }
}

void UnixTerminal::clear()
{
    if (!isATerminal)
    {
        return;
    }

    sysCls();

    // Let the parent class set the newline marker, etc.
    SimpleCommandTerminal::clear();
}

void UnixTerminal::onExit()
{
    // Signal the thread to return now instead of waiting for the destructor.
    quitInputThread = true;

    // But the thread might still be waiting on sysWaitChar(), so we
    // need to ask the user to hit another key so that the terminal
    // may shutdown properly.
    printLn("Press any key to continue...");
    if (inputThread.joinable())
    {
        inputThread.join();
    }
    printLn("");
}

void UnixTerminal::onSetClipboardString(const char * const str)
{
    clipboardString = str;
}

const char * UnixTerminal::onGetClipboardString()
{
    return clipboardString.c_str();
}

void UnixTerminal::sysCls()
{
    // Relying on system() is a major security hole, but this is
    // just a demo for the SimpleCommandTerminal, so I'm cool with it :P.
    std::system("clear");
}

int UnixTerminal::sysWaitChar()
{
    //
    // Converts any system specific console char
    // to the generic representation of SpecialKeys.
    //
    // NOTE: This function is only meant to be called
    // from the input thread.
    //
    int c = std::getchar();
    switch (c)
    {
    case '\n' : return SpecialKeys::Return;
    case '\r' : return SpecialKeys::Return;
    case 0x7F : return SpecialKeys::Backspace;
    case 0x09 : return SpecialKeys::Tab;

    // These are hacks to catch CTRL+c|v, CTRL+p, CTRL+n, CTRL+l, respectively.
    // Upper bits in the word is the CTRL key flag, lower 8 are the ASCII char.
    case 0x03 : return (SpecialKeys::Control | 'c');
    case 0x16 : return (SpecialKeys::Control | 'v');
    case 0x10 : return (SpecialKeys::Control | 'p');
    case 0x0E : return (SpecialKeys::Control | 'n');
    case 0x0C : return (SpecialKeys::Control | 'l');

    default :
        if (c == 0x1B) // Arrow key or ESCAPE:
        {
            //
            // Unlikely to be able to fix it but just to leave registered:
            //
            // There is a "bug" with the ESCAPE key.
            // To get an ESC event the user will have to type it TWICE.
            // The fist time it will enter the if above, but since both
            // ESC and the arrows return 0x1B we have to getchar() again
            // to know which key was really pressed.
            //
            // If it was an arrow key, getchar will return immediately, but if it was
            // the ESCAPE key, it will block and only return at the next key press.
            //
            // NOTE: This was only observed and tested on a Mac Laptop keyboard.
            //
            c = std::getchar();
            if (c == 0x5B)
            {
                c = std::getchar();
                switch (c)
                {
                case 0x33 :
                    {
                        // Delete is another weirdo. It produces a trailing
                        // input char we must consume before retuning.
                        std::getchar();
                        return SpecialKeys::Delete;
                    }
                case 0x41 : return SpecialKeys::UpArrow;
                case 0x42 : return SpecialKeys::DownArrow;
                case 0x43 : return SpecialKeys::RightArrow;
                case 0x44 : return SpecialKeys::LeftArrow;
                default   : break;
                } // switch (c)
            }
            return SpecialKeys::Escape;
        }
        break;
    } // switch (c)

    return c; // Any other key
}

#endif // CFG_BUILD_UNIX_TERMINAL

// ========================================================
// NativeTerminal implementation:
// ========================================================

NativeTerminal::~NativeTerminal()
{ }

NativeTerminal * NativeTerminal::createUnixTerminalInstance()
{
    #ifdef CFG_BUILD_UNIX_TERMINAL
    auto unixTerm = memAlloc<UnixTerminal>(1);
    return construct(unixTerm);
    #else // !CFG_BUILD_UNIX_TERMINAL
    return nullptr;
    #endif // CFG_BUILD_UNIX_TERMINAL
}

NativeTerminal * NativeTerminal::createWindowsTerminalInstance()
{
    #ifdef CFG_BUILD_WIN_TERMINAL
    // TODO
    return nullptr;
    #else // !CFG_BUILD_WIN_TERMINAL
    return nullptr;
    #endif // CFG_BUILD_WIN_TERMINAL
}

void NativeTerminal::destroyInstance(NativeTerminal * term)
{
    destroy(term);
    memFree(term);
}

// ================================================================================================
//
//                                  Default built-in commands
//
// ================================================================================================

#ifndef CFG_NO_DEFAULT_COMMANDS

static void printHelp(const char * const cmdName, const char * const usageArgs, SimpleCommandTerminal * term)
{
    const auto cmdManager = term->getCommandManager();
    const auto cmd = (cmdManager != nullptr ? cmdManager->findCommand(cmdName) : nullptr);

    term->print("Wrong number of arguments!\n");
    if (cmd != nullptr && *cmd->getDescCString() != '\0')
    {
        term->printF("%s: %s\nUsage: %s %s\n", cmdName, cmd->getDescCString(), cmdName, usageArgs);
    }
    else
    {
        term->printF("Usage: %s %s\n", cmdName, usageArgs);
    }
}

static int completionHandlerCVarName(const char * const partialName, std::string * outMatches,
                                     const int maxMatches, SimpleCommandTerminal * term)
{
    const auto cvarManager = term->getCVarManager();
    if (cvarManager == nullptr)
    {
        return 0;
    }

    const char * tempMatchStrings[64];
    clearArray(tempMatchStrings);

    const int matchesFound = cvarManager->findCVarsWithPartialName(partialName,
                            tempMatchStrings, lengthOfArray(tempMatchStrings));
    if (matchesFound <= 0)
    {
        return 0;
    }

    const int count = std::min(matchesFound, maxMatches);
    for (int m = 0; m < count; ++m)
    {
        outMatches[m] = tempMatchStrings[m];
    }
    return matchesFound;
}

static int completionHandlerCmdName(const char * const partialName, std::string * outMatches,
                                    const int maxMatches, SimpleCommandTerminal * term)
{
    const auto cmdManager = term->getCommandManager();
    if (cmdManager == nullptr)
    {
        return 0;
    }

    const char * tempMatchStrings[64];
    clearArray(tempMatchStrings);

    const int matchesFound = cmdManager->findCommandsWithPartialName(partialName,
                              tempMatchStrings, lengthOfArray(tempMatchStrings));
    if (matchesFound <= 0)
    {
        return 0;
    }

    const int count = std::min(matchesFound, maxMatches);
    for (int m = 0; m < count; ++m)
    {
        outMatches[m] = tempMatchStrings[m];
    }
    return matchesFound;
}

static int completionHandlerCVarOrCmdName(const char * const partialName, std::string * outMatches,
                                          const int maxMatches, SimpleCommandTerminal * term)
{
    CFG_ASSERT(partialName != nullptr);
    CFG_ASSERT(outMatches  != nullptr);
    int matchesFound = 0;

    // Built-in console commands first:
    if (*partialName != '\0')
    {
        const int builtInCount = term->getBuiltInCommandsCount();
        const int partialLen = lengthOfString(partialName);
        for (int i = 0; i < builtInCount; ++i)
        {
            if (cmdCmpNames(partialName, term->getBuiltInCommand(i)->name, partialLen) == 0)
            {
                if (matchesFound < maxMatches)
                {
                    outMatches[matchesFound] = term->getBuiltInCommand(i)->name;
                }
                ++matchesFound;
            }
        }
    }

    // User commands:
    if (matchesFound <= 0)
    {
        matchesFound = completionHandlerCmdName(partialName, outMatches, maxMatches, term);
    }

    // CVars:
    if (matchesFound <= 0)
    {
        matchesFound = completionHandlerCVarName(partialName, outMatches, maxMatches, term);
    }

    return matchesFound;
}

//
// isCVar <name>
//
// Test if the name defines a CVar.
//
static void cmdIsCVar(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("isCVar", "<name>", term);
        return;
    }

    const auto cvarManager = term->getCVarManager();
    if (cvarManager == nullptr)
    {
        return;
    }

    if (cvarManager->findCVar(args[0]))
    {
        term->printLn("yes");
    }
    else
    {
        term->printLn("no");
    }
}

//
// isCmd <name>
//
// Test if the name defines a command or a command alias.
//
static void cmdIsCmd(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("isCmd", "<name>", term);
        return;
    }

    const auto cmdManager = term->getCommandManager();
    if (cmdManager == nullptr)
    {
        return;
    }

    if (const auto cmd = cmdManager->findCommand(args[0]))
    {
        if (cmd->isAlias())
        {
            term->print("yes");
            term->setTextColor(color::cyan());
            term->print(" (command alias)\n");
            term->restoreTextColor();
        }
        else
        {
            term->printLn("yes");
        }
    }
    else
    {
        term->printLn("no");
    }
}

//
// reset <cvar>
//
// Resets the CVar to its default value.
//
static void cmdReset(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("reset", "<cvar>", term);
        return;
    }

    const auto cvarManager = static_cast<CVarManagerImpl *>(term->getCVarManager());
    if (cvarManager == nullptr)
    {
        return;
    }

    auto cvar = cvarManager->findCVar(args[0]);
    if (cvar == nullptr)
    {
        term->printF("CVar '%s' is not defined.\n", args[0]);
        return;
    }

    if (!cvarManager->internalSetDefaultValue(cvar))
    {
        term->setTextColor(color::yellow());
        term->printF("Cannot reset %s!\n", cvar->getNameCString());
        term->restoreTextColor();
    }
}

//
// toggle <cvar>
//
// Cycles the allowed values of a CVar. Toggles boolean CVars between true and false.
//
static void cmdToggle(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("toggle", "<cvar>", term);
        return;
    }

    const auto cvarManager = term->getCVarManager();
    if (cvarManager == nullptr)
    {
        return;
    }

    auto cvar = cvarManager->findCVar(args[0]);
    if (cvar == nullptr)
    {
        term->printF("CVar '%s' is not defined.\n", args[0]);
        return;
    }

    if (cvar->getType() == CVar::Type::Bool)
    {
        const bool value = cvar->getBoolValue();
        cvar->setBoolValue(!value);
    }
    else // Cycle the value strings:
    {
        std::vector<std::string> allowedValues;

        allowedValues.resize(cvar->getAllowedValueCount());
        if (cvar->getAllowedValueStrings(allowedValues.data(), static_cast<int>(allowedValues.size())) <= 0)
        {
            term->print("No values to toggle...\n");
            return;
        }

        bool toggled = false;
        const std::string currentValue = cvar->getStringValue();

        for (std::size_t v = 0; v < allowedValues.size(); ++v)
        {
            if (allowedValues[v] == currentValue)
            {
                const auto next = (v + 1) % allowedValues.size();
                if (cvar->setStringValue(allowedValues[next]))
                {
                    toggled = true;
                }
                break;
            }
        }

        if (!toggled)
        {
            term->setTextColor(color::yellow());
            term->printF("Cannot toggle %s!\n", cvar->getNameCString());
            term->restoreTextColor();
        }
    }
}

//
// set <cvar> <value> [optional flags]
//
// Where flags are one or more of:
// -persistent = CVar::Flags::Persistent
// -volatile   = CVar::Flags::volatile
// -readonly   = CVar::Flags::ReadOnly
// -initonly   = CVar::Flags::InitOnly
// -modified   = CVar::Flags::Modified
// -nocreate   = Do not create a new var if it doesn't exist.
//
// Set the value of a CVar if it is writable.
// Optionally creates the var if it doesn't exists.
// New variables are always flagged as 'UserDefined'.
//
static void cmdSet(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() < 2)
    {
        printHelp("set", "<cvar> <value> [flags: -persistent | -volatile | -readonly | -initonly | -modified | -nocreate]", term);
        return;
    }

    const auto cvarManager = static_cast<CVarManagerImpl *>(term->getCVarManager());
    if (cvarManager == nullptr)
    {
        return;
    }

    const char * varName  = args[0];
    const char * varValue = args[1];
    auto cvar = cvarManager->findCVar(varName);

    if (cvar == nullptr) // Doesn't exist:
    {
        bool noCreate = false;
        std::uint32_t varFlags = CVar::Flags::UserDefined;

        // Optional creation flags:
        if (args.getArgCount() > 2)
        {
            for (int i = 2; i < args.getArgCount(); ++i)
            {
                if (args.compare(i, "-persistent") == 0)
                {
                    varFlags |= CVar::Flags::Persistent;
                }
                else if (args.compare(i, "-volatile") == 0)
                {
                    varFlags |= CVar::Flags::Volatile;
                }
                else if (args.compare(i, "-readonly") == 0)
                {
                    varFlags |= CVar::Flags::ReadOnly;
                }
                else if (args.compare(i, "-initonly") == 0)
                {
                    varFlags |= CVar::Flags::InitOnly;
                }
                else if (args.compare(i, "-modified") == 0)
                {
                    varFlags |= CVar::Flags::Modified;
                }
                else if (args.compare(i, "-nocreate") == 0)
                {
                    noCreate = true;
                }
            }
        }

        if (noCreate)
        {
            term->printF("CVar '%s' is not defined and won't be created.\n", varName);
            return;
        }

        cvarManager->setCVarValueString(varName, varValue, varFlags);
    }
    else // Set existing:
    {
        if (!cvarManager->internalSetStringValue(cvar, varValue))
        {
            term->setTextColor(color::yellow());
            term->printF("Cannot set %s to \"%s\"!\n", varName, varValue);
            term->restoreTextColor();
        }
    }
}

//
// varAdd <cvar> <value>
// varSub <cvar> <value>
// varMul <cvar> <value>
// varDiv <cvar> <value>
//
// Adds/subtract, multiply/divide a value with a numeric CVar.
// Does nothing for strings, enums or booleans.
//
template<template<typename> class OP>
static void cvarValueOp(const char * const opName, const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 2)
    {
        printHelp(opName, "<cvar> <value>", term);
        return;
    }

    const auto cvarManager = term->getCVarManager();
    if (cvarManager == nullptr)
    {
        return;
    }

    auto cvar = cvarManager->findCVar(args[0]);
    if (cvar == nullptr)
    {
        term->printF("CVar '%s' is not defined.\n", args[0]);
        return;
    }

    if (cvar->getType() != CVar::Type::Int &&
        cvar->getType() != CVar::Type::Float)
    {
        term->printF("Cannot %s to value of non-numeric CVar.\n", opName);
        return;
    }

    const double argVal = std::strtod(args[1], nullptr);
    const double varVal = cvar->getFloatValue();

    OP<double> op;
    if (!cvar->setFloatValue(op(varVal, argVal)))
    {
        term->setTextColor(color::yellow());
        term->printF("Cannot %s to value of CVar %s!\n", opName, cvar->getNameCString());
        term->restoreTextColor();
    }
}

static void cmdVarAdd(const CommandArgs & args, SimpleCommandTerminal * term) { cvarValueOp< std::plus       >("varAdd", args, term); }
static void cmdVarSub(const CommandArgs & args, SimpleCommandTerminal * term) { cvarValueOp< std::minus      >("varSub", args, term); }
static void cmdVarMul(const CommandArgs & args, SimpleCommandTerminal * term) { cvarValueOp< std::multiplies >("varMul", args, term); }
static void cmdVarDiv(const CommandArgs & args, SimpleCommandTerminal * term) { cvarValueOp< std::divides    >("varDiv", args, term); }

//
// alias <name> <string> <mode> [optional description]
//
// Where mode is:
// -append    = CommandExecMode::Append (push at buffer tail;  buffered)
// -insert    = CommandExecMode::Insert (push at buffer front; buffered)
// -immediate = CommandExecMode::Immediate (immediate execution; non-buffered)
//
// Create an alias for a command string.
// The command alias cannot take any arguments when invoked.
//
static void cmdAlias(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() < 3)
    {
        printHelp("alias", "<name> <command string> <mode: -append | -insert | -immediate> [optional description]", term);
        return;
    }

    const auto cmdManager = term->getCommandManager();
    if (cmdManager == nullptr)
    {
        return;
    }

    const char * aliasNameStr  = args[0];
    const char * aliasedCmdStr = args[1];
    const char * description   = ((args.getArgCount() > 3) ? args[3] : "");

    CommandExecMode execModeFlag;
    if (args.compare(2, "-append") == 0)
    {
        execModeFlag = CommandExecMode::Append;
    }
    else if (args.compare(2, "-insert") == 0)
    {
        execModeFlag = CommandExecMode::Insert;
    }
    else if (args.compare(2, "-immediate") == 0)
    {
        execModeFlag = CommandExecMode::Immediate;
    }
    else
    {
        execModeFlag = CommandExecMode::Append;
        term->setTextColor(color::yellow());
        term->printF("Unrecognized flag \"%s\". Defaulting to '-append'...\n", args[2]);
        term->restoreTextColor();
    }

    if (!cmdManager->createCommandAlias(aliasNameStr, aliasedCmdStr, execModeFlag, description))
    {
        term->setTextColor(color::yellow());
        term->print("Failed to create new command alias!\n");
        term->restoreTextColor();
        return;
    }

    term->printF("New command alias '%s' created successfully.\n", aliasNameStr);
}

//
// unalias <name | -all>
//
// Removes the command alias. Fails if 'name' is a normal command or CVar.
// If instead of a name the flag "-all" is passed, all registered aliases are removed.
//
static void cmdUnAlias(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("unalias", "<name | -all>", term);
        return;
    }

    const auto cmdManager = term->getCommandManager();
    if (cmdManager == nullptr)
    {
        return;
    }

    if (args.compare(0, "-all") == 0)
    {
        cmdManager->removeAllCommandAliases();
        term->print("All command aliases removed.\n");
    }
    else
    {
        if (!cmdManager->removeCommandAlias(args[0]))
        {
            term->printF("'%s' is not a command alias.\n", args[0]);
            return;
        }
        term->print("Command alias removed.\n");
    }
}

//
// print <cvar>
//
// Print CVar value, flags and description.
//
static void cmdPrint(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("print", "<cvar>", term);
        return;
    }

    const auto cvarManager = term->getCVarManager();
    if (cvarManager == nullptr)
    {
        return;
    }

    const auto cvar = cvarManager->findCVar(args[0]);
    if (cvar == nullptr)
    {
        term->printF("CVar '%s' is not defined.\n", args[0]);
        return;
    }

    term->printF("%s = %s;", cvar->getNameCString(), cvar->getStringValue().c_str());

    const std::string flags = cvar->getFlagsString();
    if (!flags.empty())
    {
        term->printF("  flags:'%s';", flags.c_str());
    }

    const std::string type = cvar->getTypeString();
    if (!type.empty())
    {
        term->printF("  type:%s;", type.c_str());
    }

    const CVar::Type tag = cvar->getType();
    if (tag == CVar::Type::Int || tag == CVar::Type::Float)
    {
        std::string range[2];
        cvar->getAllowedValueStrings(range, lengthOfArray(range));
        term->printF("  range:[%s, %s];", range[0].c_str(), range[1].c_str());
    }

    const std::string defaultVal = cvar->getDefaultValueString();
    if (!defaultVal.empty())
    {
        term->printF("  default:%s;", defaultVal.c_str());
    }

    const std::string desc = cvar->getDesc();
    if (!desc.empty())
    {
        term->printF("  description:\"%s\";", desc.c_str());
    }

    term->print("\n\n");
}

//
// help <command | cvar>
//
// Prints the description comment of the given command or CVar.
//
static void cmdHelp(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() != 1)
    {
        printHelp("help", "<command | cvar>", term);
        return;
    }

    std::string description;

    // If this the name of a command, print its help text:
    const auto cmdManager = term->getCommandManager();
    const auto cmd = (cmdManager != nullptr ? cmdManager->findCommand(args[0]) : nullptr);
    if (cmd != nullptr)
    {
        description = cmd->getDesc();
        if (description.empty())
        {
            description = "No description provided.";
        }

        term->setTextColor(color::cyan());
        term->printF("%s: ", cmd->getNameCString());

        term->restoreTextColor();
        term->printF("%s\n", description.c_str());
        return;
    }

    // Not a command, name might refer to a CVar:
    const auto cvarManager = term->getCVarManager();
    const auto cvar = (cvarManager != nullptr ? cvarManager->findCVar(args[0]) : nullptr);
    if (cvar != nullptr)
    {
        description = cvar->getDesc();
        if (description.empty())
        {
            description = "No description provided.";
        }

        term->setTextColor(color::cyan());
        term->printF("%s: ", cvar->getNameCString());

        term->restoreTextColor();
        term->printF("%s\n", description.c_str());
        return;
    }

    // Could also be a console built-in:
    if (const auto builtIn = term->getBuiltInCommand(args[0]))
    {
        term->setTextColor(color::cyan());
        term->printF("%s: ", builtIn->name);

        term->restoreTextColor();
        term->printF("%s\n", builtIn->desc);
        return;
    }

    // Not a command nor a CVar:
    term->printF("No command or CVar found with name \"%s\".\n", args[0]);
}

//
// echo [string | $(cvar)]
//
// Echoes the given arguments to the terminal.
// If no args provided, just prints a blank line.
//
static void cmdEcho(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.isEmpty())
    {
        term->print("\n");
        return;
    }

    for (const char * arg : args)
    {
        term->printF("%s ", arg);
    }
    term->print("\n");
}

// Substring search helper for command and CVar listing.
// Returns true if the substring is found.
static bool findSubstring(const char * const str,    const int strLength,
                          const char * const substr, const int subLength,
                          const bool ignoreCase)
{
    CFG_ASSERT(str    != nullptr);
    CFG_ASSERT(substr != nullptr);

    char tempStr[1024];
    char tempSubstr[1024];

    if (strLength >= lengthOfArray(tempStr) ||
        subLength >= lengthOfArray(tempSubstr))
    {
        return -1;
    }

    if (!ignoreCase) // Case-sensitive search:
    {
        // substr not required to be null terminated,
        // so we need to copy and put a \0 at the end.
        std::memcpy(tempSubstr, substr, subLength);
        tempSubstr[subLength] = '\0';

        return std::strstr(str, tempSubstr) != nullptr;
    }
    else // Search ignoring character case:
    {
        // Cast both to lowercase first, then search:
        int i;
        for (i = 0; i < strLength; ++i)
        {
            tempStr[i] = std::tolower(str[i]);
        }
        tempStr[i] = '\0';

        for (i = 0; i < subLength; ++i)
        {
            tempSubstr[i] = std::tolower(substr[i]);
        }
        tempSubstr[i] = '\0';

        return std::strstr(tempStr, tempSubstr) != nullptr;
    }
}

//
// listCmds [search pattern] [-sort]
//
// Prints a list of commands.
// If a search pattern is provided, prints only commands
// that have the search pattern as a substring. Appending
// "/i" to the end of the pattern makes the substring
// search case-insensitive. Example:
//
// "var/i"
//
// Searches for all commands that have the substring "var"
// in the name, with all possible word-case combinations.
//
static void cmdListCmds(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() > 2)
    {
        printHelp("listCmds", "[search-pattern [/i]] [-sort]", term);
        return;
    }

    const auto cmdManager = term->getCommandManager();
    if (cmdManager == nullptr)
    {
        return;
    }

    struct CmdPtr
    {
        const Command                           * userCmd;
        const SimpleCommandTerminal::BuiltInCmd * builtInCmd;
    };
    struct CmdList
    {
        const char *        searchPattern    = nullptr;
        int                 searchPatternLen = 0;
        int                 longestCmdName   = 0;
        bool                ignoreCase       = false;
        std::vector<CmdPtr> patternMatching;
    };
    CmdList cmdList{};

    // Optional search pattern:
    if (!args.isEmpty() && args.compare(0, "-sort") != 0)
    {
        cmdList.searchPattern    = args[0];
        cmdList.searchPatternLen = lengthOfString(cmdList.searchPattern);

        if (cmdList.searchPatternLen > 2 &&
            cmdList.searchPattern[cmdList.searchPatternLen - 2] == '/' &&
            cmdList.searchPattern[cmdList.searchPatternLen - 1] == 'i')
        {
            cmdList.ignoreCase = true;
            cmdList.searchPatternLen -= 2; // Don't include the "/i"
        }
    }

    // We will have up to the number of registered command + the built-ins in the
    // match list, so we might as well reserve memory for the worst case beforehand.
    // User is more likely to call listCmds without a search pattern.
    cmdList.patternMatching.reserve(
            cmdManager->getRegisteredCommandsCount() +
            term->getBuiltInCommandsCount());

    //
    // Gather the matching commands:
    //
    cmdManager->enumerateAllCommands(
            [](Command * cmd, void * userContext)
            {
                auto list = static_cast<CmdList *>(userContext);
                const int nameLen = lengthOfString(cmd->getNameCString());

                if (list->searchPattern != nullptr)
                {
                    if (findSubstring(cmd->getNameCString(), nameLen,
                                      list->searchPattern, list->searchPatternLen,
                                      list->ignoreCase))
                    {
                        if (nameLen > list->longestCmdName)
                        {
                            list->longestCmdName = nameLen;
                        }
                        list->patternMatching.push_back({ cmd, nullptr });
                    }
                }
                else
                {
                    if (nameLen > list->longestCmdName)
                    {
                        list->longestCmdName = nameLen;
                    }
                    list->patternMatching.push_back({ cmd, nullptr });
                }
                return true;
            },
            &cmdList);

    //
    // Add the SimpleCommandTerminal built-ins to the list as well:
    //
    const int builtInCount = term->getBuiltInCommandsCount();
    for (int i = 0; i < builtInCount; ++i)
    {
        const auto cmd    = term->getBuiltInCommand(i);
        const int nameLen = lengthOfString(cmd->name);

        if (cmdList.searchPattern != nullptr)
        {
            if (findSubstring(cmd->name, nameLen, cmdList.searchPattern,
                              cmdList.searchPatternLen, cmdList.ignoreCase))
            {
                if (nameLen > cmdList.longestCmdName)
                {
                    cmdList.longestCmdName = nameLen;
                }
                cmdList.patternMatching.push_back({ nullptr, cmd });
            }
        }
        else
        {
            if (nameLen > cmdList.longestCmdName)
            {
                cmdList.longestCmdName = nameLen;
            }
            cmdList.patternMatching.push_back({ nullptr, cmd });
        }
    }

    if (cmdList.patternMatching.empty())
    {
        if (cmdList.searchPattern != nullptr)
        {
            term->printF("No matching commands found for pattern \"%s\".\n", cmdList.searchPattern);
        }
        else
        {
            term->print("No commands found.\n");
        }
        return;
    }

    // Sort alphabetically (optional):
    // "-sort" can appear right after the commands name or after the search pattern.
    if (!args.isEmpty() && (args.compare(0, "-sort") == 0 ||
                            args.compare(1, "-sort") == 0))
    {
        std::sort(std::begin(cmdList.patternMatching), std::end(cmdList.patternMatching),
                  [](const CmdPtr & a, const CmdPtr & b)
                  {
                      const char * aName = (a.userCmd != nullptr ? a.userCmd->getNameCString() : a.builtInCmd->name);
                      const char * bName = (b.userCmd != nullptr ? b.userCmd->getNameCString() : b.builtInCmd->name);
                      return cmdCmpNames(aName, bName) < 0;
                  });
    }

    //
    // Print a list in simple table format:
    //
    // cmdName  "description"
    //
    term->print("================ Command Listing ================\n");

    for (const CmdPtr ptr : cmdList.patternMatching)
    {
        if (ptr.userCmd != nullptr)
        {
            const auto cmd = ptr.userCmd;

            if (cmd->isAlias())
            {
                term->setTextColor(color::magenta());
            }

            term->printF("%-*s ", cmdList.longestCmdName, cmd->getNameCString());

            if (cmd->isAlias())
            {
                term->restoreTextColor();
            }

            if (*cmd->getDescCString() != '\0')
            {
                term->printF(" \"%s\"", cmd->getDescCString());
            }
        }
        else if (ptr.builtInCmd != nullptr)
        {
            const auto cmd = ptr.builtInCmd;

            term->setTextColor(color::white());
            term->printF("%-*s ", cmdList.longestCmdName, cmd->name);
            term->restoreTextColor();

            if (*cmd->desc != '\0')
            {
                term->printF(" \"%s\"", cmd->desc);
            }
        }
        term->print("\n");
    }

    term->setTextColor(color::cyan());
    term->printF("listed %u commands.\n\n", static_cast<unsigned>(cmdList.patternMatching.size()));

    term->setTextColor(color::magenta());
    term->print("magenta");
    term->restoreTextColor();
    term->print(" = command aliases\n");

    term->setTextColor(color::white());
    term->print("white  ");
    term->restoreTextColor();
    term->print(" = built-in commands\n");

    term->print("=================================================\n");
}

//
// listCVars [search pattern] [-sort] [-values]
//
// Same as listCmds in regard to the search pattern.
// "-sort" flag sorts the resulting list alphabetically.
// "-values" flag prints a shorter name+value summary for each variable.
//
static void cmdListCVars(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() > 3)
    {
        printHelp("listCVars", "[search-pattern[/i]] [-sort] [-values]", term);
        return;
    }

    const auto cvarManager = term->getCVarManager();
    if (cvarManager == nullptr)
    {
        return;
    }

    struct CVarList
    {
        const char * searchPattern    = nullptr;
        int          searchPatternLen = 0;
        int          longestCVarName  = 0;
        bool         ignoreCase       = false;
        std::vector<const CVar *> patternMatching;
    };
    CVarList cvarList{};

    // Optional search pattern:
    if (!args.isEmpty() && args.compare(0, "-sort") != 0 && args.compare(0, "-values") != 0)
    {
        cvarList.searchPattern    = args[0];
        cvarList.searchPatternLen = lengthOfString(cvarList.searchPattern);

        if (cvarList.searchPatternLen > 2 &&
            cvarList.searchPattern[cvarList.searchPatternLen - 2] == '/' &&
            cvarList.searchPattern[cvarList.searchPatternLen - 1] == 'i')
        {
            cvarList.ignoreCase = true;
            cvarList.searchPatternLen -= 2; // Don't include the "/i"
        }
    }

    //
    // Gather the matching CVars:
    //
    cvarManager->enumerateAllCVars(
            [](CVar * cvar, void * userContext)
            {
                auto list = static_cast<CVarList *>(userContext);
                const int nameLen = lengthOfString(cvar->getNameCString());

                if (list->searchPattern != nullptr)
                {
                    if (findSubstring(cvar->getNameCString(), nameLen,
                                      list->searchPattern, list->searchPatternLen,
                                      list->ignoreCase))
                    {
                        if (nameLen > list->longestCVarName)
                        {
                            list->longestCVarName = nameLen;
                        }
                        list->patternMatching.push_back(cvar);
                    }
                }
                else
                {
                    if (nameLen > list->longestCVarName)
                    {
                        list->longestCVarName = nameLen;
                    }
                    list->patternMatching.push_back(cvar);
                }
                return true;
            },
            &cvarList);

    if (cvarList.patternMatching.empty())
    {
        if (cvarList.searchPattern != nullptr)
        {
            term->printF("No matching CVars found for pattern \"%s\".\n", cvarList.searchPattern);
        }
        else
        {
            term->print("No CVars found.\n");
        }
        return;
    }

    // Sort alphabetically (optional):
    // "-sort" can appear right after the commands name or after the search pattern.
    if (!args.isEmpty() && (args.compare(0, "-sort") == 0 ||
                            args.compare(1, "-sort") == 0 ||
                            args.compare(2, "-sort") == 0))
    {
        std::sort(std::begin(cvarList.patternMatching), std::end(cvarList.patternMatching),
                  [](const CVar * a, const CVar * b)
                  {
                      return a->compareNames(*b);
                  });
    }

    // Third optional parameter. If set, print only the names and values.
    const bool valuesOnly = !args.isEmpty() && (args.compare(0, "-values") == 0 ||
                                                args.compare(1, "-values") == 0 ||
                                                args.compare(2, "-values") == 0);

    //
    // Print a list in simple table format:
    //
    term->print("================== CVar Listing =================\n");

    if (valuesOnly) // Value print:
    {
        for (const auto cvar : cvarList.patternMatching)
        {
            term->printF("%-*s \"%s\"\n", cvarList.longestCVarName, cvar->getNameCString(), cvar->getStringValue().c_str());
        }
    }
    else // Verbose info print:
    {
        for (const auto cvar : cvarList.patternMatching)
        {
            term->printF("%-*s | %-6s | %-11s |",
                         cvarList.longestCVarName,
                         cvar->getNameCString(),
                         cvar->getTypeCString(),
                         cvar->getFlagsString().c_str());

            if (*cvar->getDescCString() != '\0')
            {
                term->printF(" \"%s\"", cvar->getDescCString());
            }

            term->print("\n");
        }
    }

    term->setTextColor(color::cyan());
    term->printF("listed %u variables.\n", static_cast<unsigned>(cvarList.patternMatching.size()));

    if (!valuesOnly)
    {
        term->print("\n");
        term->printLn("Flags reference:");
        term->printLn("M = Modified");
        term->printLn("P = Persistent");
        term->printLn("V = Volatile");
        term->printLn("R = Read only");
        term->printLn("I = Init only");
        term->printLn("C = Range check");
        term->printLn("U = User defined");
        term->printLn("0 = No flags");
    }

    term->restoreTextColor();
    term->print("=================================================\n");
}

//
// saveConfig [filename]
//
// Writes all modified (persistent) CVars to file and clears the modified
// flag of each. The filename is optional. If not provided, a default name
// will be used. Command aliases will also be saved.
//
// Warning: Exiting file is overwritten.
//
static void cmdSaveConfig(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() > 1)
    {
        printHelp("saveConfig", "[filename]", term);
        return;
    }

    const auto cmdManager  = term->getCommandManager();
    const auto cvarManager = term->getCVarManager();
    const char * filename  = (!args.isEmpty() ? args[0] : CFG_DEFAULT_CONFIG_FILE);

    FileIOCallbacks * io = getFileIOCallbacks();

    FileHandle fileIn;
    if (!io->open(&fileIn, filename, FileOpenMode::Write))
    {
        return;
    }

    //
    // Write the header comment:
    //
    io->writeString(fileIn,
                    "#\n"
                    "# File automatically generated by lib config;\n"
                    "# Do not modify.\n"
                    "#\n");

    //
    // Write the CVar 'set' commands:
    //
    if (cvarManager != nullptr && cvarManager->getRegisteredCVarsCount() > 0)
    {
        io->writeString(fileIn, "\n# CVars:\n");

        cvarManager->enumerateAllCVars(
                [](CVar * cvar, void * fileHandle)
                {
                    if (cvar->isPersistent())
                    {
                        FileIOCallbacks * iocb = getFileIOCallbacks();
                        auto var = static_cast<const CVarImplBase *>(cvar);

                        char tempStr[MaxCommandArgStrLength];
                        iocb->writeFormat(fileHandle, "%s\n", var->toCfgString(tempStr, MaxCommandArgStrLength));
                    }

                    // Variable was synchronized with the persistent
                    // storage, so we can now clear the modified flag.
                    cvar->clearModified();
                    return true;
                },
                fileIn);
    }

    //
    // White the command aliases:
    //
    if (cmdManager != nullptr && cmdManager->getCommandAliasCount() > 0)
    {
        io->writeString(fileIn, "\n# Command aliases:\n");

        cmdManager->enumerateAllCommands(
                [](Command * cmd, void * fileHandle)
                {
                    if (cmd->isAlias())
                    {
                        FileIOCallbacks * iocb = getFileIOCallbacks();
                        auto alias = static_cast<const CommandImplAlias *>(cmd);

                        char tempStr[MaxCommandArgStrLength];
                        iocb->writeFormat(fileHandle, "%s\n", alias->toCfgString(tempStr, MaxCommandArgStrLength));
                    }
                    return true;
                },
                fileIn);
    }

    io->writeString(fileIn, "\n");
    io->close(fileIn);

    term->printF("Config file \"%s\" successfully written.\n", filename);
}

//
// reloadConfig [filename] [-echo] [-force]
//
// Loads the configuration file, possibly overwriting the values of currently modified CVars.
// If there are pending persistent CVars that are not saved yet, this command fails with a
// warning. To force reloading you can call it again with "-force". Running this command
// will allow updating ReadOnly and InitOnly CVars alike.
//
// "-echo" will print each command in the configuration file to the terminal screen.
// The filename is optional. If not provided, a default name will be used.
//
static void cmdReloadConfig(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() > 3)
    {
        printHelp("reloadConfig", "[filename] [-echo] [-force]", term);
        return;
    }

    const auto cmdManager  = term->getCommandManager();
    const auto cvarManager = static_cast<CVarManagerImpl *>(term->getCVarManager());

    if (cmdManager == nullptr || cvarManager == nullptr)
    {
        return;
    }

    const char * filename;
    if (args.isEmpty() ||
        args.compare(0, "-echo")  == 0 ||
        args.compare(0, "-force") == 0)
    {
        // No filename provided or just the flags.
        filename = CFG_DEFAULT_CONFIG_FILE;
    }
    else
    {
        filename = args[0];
    }

    bool consoleEcho = false;
    bool forceReload = false;
    for (int i = 0; i < args.getArgCount(); ++i)
    {
        if (args.compare(i, "-echo") == 0)
        {
            consoleEcho = true;
        }
        else if (args.compare(i, "-force") == 0)
        {
            forceReload = true;
        }
    }

    //
    // Count the modified CVars.
    // If there's any, we will refuse overwriting unless -force is set.
    //
    bool hasModifiedVars = false;
    cvarManager->enumerateAllCVars(
            [](CVar * cvar, void * userContext)
            {
                if (cvar->isModified())
                {
                    auto pHasModifiedVars = static_cast<bool *>(userContext);
                    (*pHasModifiedVars) = true;
                    return false; // We can stop iterating now.
                }
                return true;
            },
            &hasModifiedVars);

    if (hasModifiedVars && !forceReload)
    {
        term->setTextColor(color::yellow());
        term->print("There are pending modifications on CVars that haven't been saved yet; Stopping.\n");
        term->print("To force a reload use: \"reloadConfig [filename] -force\".\n");
        term->restoreTextColor();
        return;
    }

    //
    // Load and run the configuration file:
    //
    // ReadOnly and InitOnly CVars can also be updated by this.
    //
    cvarManager->setAllowWritingReadOnlyVars(true);
    if (!cmdManager->execConfigFile(filename, (consoleEcho ? term : nullptr)))
    {
        term->setTextColor(color::red());
        term->printF("Failed to reload config file \"%s\".\n", filename);
        term->restoreTextColor();
    }
    else
    {
        term->printF("Config file \"%s\" successfully loaded.\n", filename);
    }
    cvarManager->setAllowWritingReadOnlyVars(false);
}

//
// exec <config-file | command-string> [-echo]
//
// Executes the first argument. If it is a filename ended in ".cfg" or ".ini",
// it is loaded and executed as a configuration file. Otherwise, the string is
// appended in the CommandManager buffer for later execution as a command line.
//
// If "-echo" is passed after the filename/string, the commands are echoed to the terminal.
//
static void cmdExec(const CommandArgs & args, SimpleCommandTerminal * term)
{
    if (args.getArgCount() < 1 || args.getArgCount() > 2)
    {
        printHelp("exec", "<config-file | command-string> [-echo]", term);
        return;
    }
    if (args.compare(0, "-echo") == 0) // Flag in the wrong place?
    {
        term->print("Expected filename or command string after 'exec' command.\n");
        return;
    }

    const auto cmdManager = term->getCommandManager();
    if (cmdManager == nullptr)
    {
        return;
    }

    const char * execString  = args[0];
    const bool   consoleEcho = (args.compare(1, "-echo") == 0);
    bool         isFilename  = false;

    //
    // Simple test: If the string ends with one of the expected
    // filename extensions, we assume it is a configuration file.
    //
    const char * const cfgFileExtensions[]{ "cfg", "ini" };
    for (int i = 0; i < lengthOfArray(cfgFileExtensions); ++i)
    {
        if (const char * ptr = std::strrchr(execString, '.'))
        {
            if (std::strcmp(ptr + 1, cfgFileExtensions[i]) == 0)
            {
                isFilename = true;
                break;
            }
        }
    }

    if (isFilename)
    {
        term->printF("Executing config file \"%s\"...\n", execString);

        // Config files are executed immediately.
        if (!cmdManager->execConfigFile(execString, (consoleEcho ? term : nullptr)))
        {
            term->setTextColor(color::red());
            term->printF("Failed to exec config file \"%s\".\n", execString);
            term->restoreTextColor();
            return;
        }

        term->print("Done!\n");
    }
    else
    {
        term->printF("Appending command line \"%s\"...\n", execString);
        cmdManager->execAppend(execString); // Command strings will be always buffered.
    }
}

#endif // CFG_NO_DEFAULT_COMMANDS

// ========================================================
// Default commands registration:
// ========================================================

void registerDefaultCommands(CommandManager * cmdManager, SimpleCommandTerminal * term)
{
    #ifndef CFG_NO_DEFAULT_COMMANDS
    CFG_ASSERT(cmdManager != nullptr);
    CFG_ASSERT(term       != nullptr);

    // The command handlers take a void* as the user-data, but I want a SimpleCommandTerminal* for these...
    auto makeCmdHandler = [](void (*func)(const CommandArgs &, SimpleCommandTerminal *))
    {
        return reinterpret_cast<CommandHandlerCallback>(func);
    };
    auto makeCompletionHandler = [](int (*func)(const char *, std::string *, int, SimpleCommandTerminal *))
    {
        return reinterpret_cast<CommandArgCompletionCallback>(func);
    };
    const auto nullCompletionHandler = nullptr;

    //
    // The default commands:
    //

    cmdManager->registerCommand("print", makeCmdHandler(&cmdPrint), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Print CVar value, flags and description.");

    cmdManager->registerCommand("help", makeCmdHandler(&cmdHelp), makeCompletionHandler(&completionHandlerCVarOrCmdName),
                                term, "Prints a description comment for the given command or CVar.");

    cmdManager->registerCommand("echo", makeCmdHandler(&cmdEcho), nullCompletionHandler,
                                term, "Echoes the given arguments to the terminal. If no args provided, prints a blank line.");

    cmdManager->registerCommand("alias", makeCmdHandler(&cmdAlias), nullCompletionHandler,
                                term, "Create an alias for a command string.");

    cmdManager->registerCommand("unalias", makeCmdHandler(&cmdUnAlias), nullCompletionHandler,
                                term, "Removes the command alias. Does nothing if the name refers to a command or CVar.");

    cmdManager->registerCommand("isCmd", makeCmdHandler(&cmdIsCmd), nullCompletionHandler,
                                term, "Test if the name defines a command or a command alias.");

    cmdManager->registerCommand("isCVar", makeCmdHandler(&cmdIsCVar), nullCompletionHandler,
                                term, "Test if the name defines a CVar.");

    cmdManager->registerCommand("reset", makeCmdHandler(&cmdReset), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Resets the CVar to its default value.");

    cmdManager->registerCommand("toggle", makeCmdHandler(&cmdToggle), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Cycles the allowed values of a CVar. Toggles boolean CVars between true and false.");

    cmdManager->registerCommand("set", makeCmdHandler(&cmdSet), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Set the value of a CVar if it is writable. Optionally creates the var if it doesn't exists.");

    cmdManager->registerCommand("varAdd", makeCmdHandler(&cmdVarAdd), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Adds a value to a numeric CVar. Does nothing for strings, enums or booleans.");

    cmdManager->registerCommand("varSub", makeCmdHandler(&cmdVarSub), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Subtract a value from a numeric CVar. Does nothing for strings, enums or booleans.");

    cmdManager->registerCommand("varMul", makeCmdHandler(&cmdVarMul), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Multiply a value to a numeric CVar. Does nothing for strings, enums or booleans.");

    cmdManager->registerCommand("varDiv", makeCmdHandler(&cmdVarDiv), makeCompletionHandler(&completionHandlerCVarName),
                                term, "Divide a value with a numeric CVar. Does nothing for strings, enums or booleans.");

    cmdManager->registerCommand("listCmds", makeCmdHandler(&cmdListCmds), nullCompletionHandler,
                                term, "Prints a list of the available commands.");

    cmdManager->registerCommand("listCVars", makeCmdHandler(&cmdListCVars), nullCompletionHandler,
                                term, "Prints a list of the registered CVars.");

    cmdManager->registerCommand("saveConfig", makeCmdHandler(&cmdSaveConfig), nullCompletionHandler,
                                term, "Writes a configuration file with the registered CVars and command aliases. Clears modified flags.");

    cmdManager->registerCommand("reloadConfig", makeCmdHandler(&cmdReloadConfig), nullCompletionHandler,
                                term, "Loads a configuration file updating existing CVars and possibly creating new ones.");

    cmdManager->registerCommand("exec", makeCmdHandler(&cmdExec), nullCompletionHandler,
                                term, "Execute a command string or a configuration file.");
    #else // CFG_NO_DEFAULT_COMMANDS
    (void)cmdManager;
    (void)term;
    #endif // CFG_NO_DEFAULT_COMMANDS
}

} // namespace cfg {}
