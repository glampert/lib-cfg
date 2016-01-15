
// ================================================================================================
// -*- C++ -*-
// File: cfg_utils.hpp
// Author: Guilherme R. Lampert
// Created on: 03/11/15
// Brief: Misc utilities and helpers for the CFG library. This is an use internal file.
// ================================================================================================

#ifndef CFG_UTILS_HPP
#define CFG_UTILS_HPP

#include <cstddef>   // For std::size_t, basic types, etc
#include <cctype>    // For std::tolower/toupper
#include <cstring>   // For std::memcpy, std::strlen, etc
#include <utility>   // For std::swap, std::move
#include <algorithm> // For std::sort, std::min/max

//
// Overridable assert() macro for CFG:
//
#ifndef CFG_ASSERT
    #include <cassert>
    #define CFG_ASSERT assert
#endif // CFG_ASSERT

//TODO TEMP
#define CFG_CVAR_WANTS_STD_STRING_CONVERSION 1
#define CFG_CMD_WANTS_STD_FUNCTION_DELEGATES 1

// CVar name comparisons/lookup are case-sensitive if this is defined.
#define CFG_CVAR_CASE_SENSITIVE_NAMES 1

// CVar strings such as the allowed values and boolean values are case-sensitive if this is defined.
#define CFG_CVAR_CASE_SENSITIVE_STRINGS 1

// Command name comparisons/lookup are case-sensitive if this is defined.
#define CFG_CMD_CASE_SENSITIVE_NAMES 1

// ========================================================
// Macros are always prefixed with the library namespace!
// ========================================================

//TODO c++98 compat!
#define CFG_DISABLE_COPY_ASSIGN(className) \
  private:                                 \
    className(const className &) = delete; \
    className & operator = (const className &) = delete

//TODO need a custom allocator...
//
// Preferably, should get rid of these macros.
// Probably define class-level new/delete for the classes that get dynamically allocated.
//
#define CFG_NEW new
#define CFG_DELETE delete

#include <cstdio>//TEMP for printf

// Visual Studio complains about this constant expression with Level-4 warnings enabled.
// Using the comma operator tricks it. Great job MS, spurious warning and it doesn't even work properly!
#ifdef _MSC_VER
#define CFG_ENDMACRO while (0,0)
#else // !_MSC_VER
#define CFG_ENDMACRO while (0)
#endif // _MSC_VER

//TEMP
namespace cfg
{
extern bool silentErrors;
} // namespace cfg {}
#define CFG_ERROR(...) do { if (!cfg::silentErrors) {std::printf(__VA_ARGS__); std::printf("\n");} } CFG_ENDMACRO

//TODO
// C++98 compat:
//
#define CFG_NULL nullptr
#define CFG_FINAL_CLASS final
#define CFG_OVERRIDE override
// must define this to nothing if unavailable. only used for functions
#define CFG_CONSTEXPR_FUNC constexpr
////////////////

//TODO replace with user supplied callbacks!
#include <cstdlib>
namespace cfg
{
template<class T>
inline T * memAlloc(const std::size_t countInItems)
{
    CFG_ASSERT(countInItems != 0);
    return reinterpret_cast<T *>(std::malloc(countInItems * sizeof(T)));
}
template<class T>
inline void memFree(T * ptr)
{
    if (ptr != CFG_NULL)
    {
        // The incoming pointer may be const, hence the C-style cast.
        std::free((void *)ptr);
    }
}
} // namespace cfg {}

//
// NOTE can make these actual inline/template functions.
// Leave uppercase and the macros as a fallback if the
// compiler has problems with them... (like old CGG with the sized templates)
//

// Don't care about 32-64bit truncation. Our strings are not nearly that long.
#define CFG_ISTRLEN(str) static_cast<int>(std::strlen(str))
// FIXME probably replace with a lengthOf() function like I did in NTB

//
// Memset wrappers to avoid the var/sizeof() repetition:
//
#define CFG_ARRAY_LEN(arr)                static_cast<int>(sizeof(arr) / sizeof((arr)[0]))
#define CFG_CLEAR_ARRAY(arr)              std::memset((arr), 0, sizeof(arr))
#define CFG_CLEAR_ARRAY_SIZED(arr, count) std::memset((arr), 0, sizeof((arr)[0]) * (count))

//
// namespace cfg -> Public namespace for the CFG Library.
//
namespace cfg
{

// ========================================================
// Common utility functions:
// ========================================================

inline bool isWhitespace(const int chr)
{
    // '\f' and '\v' ignored on purpose.
    return (chr == ' ' || chr == '\t' || chr == '\n' || chr == '\r');
}

char * cloneString(const char * src);
char * rightTrimString(char * strIn);
int copyString(char * dest, int destSizeInChars, const char * source);
int compareStringsNoCase(const char * s1, const char * s2, unsigned int count = ~0u);

struct BoolCStr
{
    const char * trueStr;
    const char * falseStr;
};

const BoolCStr * getBoolStrings();
void setBoolStrings(const BoolCStr * strings);

// ========================================================
// StringHasher Functor:
//
// Simple and fast One-at-a-Time (OAT) hash algorithm
// http://en.wikipedia.org/wiki/Jenkins_hash_function
// ========================================================

struct StringHasher CFG_FINAL_CLASS
{
    unsigned int operator()(const char * str) const
    {
        CFG_ASSERT(str != CFG_NULL);

        unsigned int h = 0;
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

// ========================================================
// StringHasherNoCase Functor:
//
// Simple and fast One-at-a-Time (OAT) hash algorithm
// http://en.wikipedia.org/wiki/Jenkins_hash_function
// ========================================================

struct StringHasherNoCase CFG_FINAL_CLASS
{
    unsigned int operator()(const char * str) const
    {
        CFG_ASSERT(str != CFG_NULL);

        unsigned int h = 0;
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
// Commands and CVars are stored inside a linked hash-table
// to accelerate lookup by name. We use an intrusive data
// structure to avoid additional memory allocations, so
// CommandHandler and CVar classes must inherit from this
// node type to be able to insert themselves into the HT.
// ========================================================

template<class T>
class HashTableLink
{
public:

    // Hash-table needs access to internal data of its nodes.
    template<class IT, class HF>
    friend class LinkedHashTable;

    HashTableLink()
        : hashNext(CFG_NULL)
        , listNext(CFG_NULL)
        , hashKey(0)
    { }

    // Allows iterating the nodes in linked-list style.
    T * getNext() const { return listNext; }

    // Publicly available for debug printing.
    unsigned int getHashKey() const { return hashKey; }

protected:

    // Hash links are not meant to be directly deleted,
    // so we don't need a public virtual destructor.
    ~HashTableLink() { }

    T * hashNext; // Link to next element in the hash-table bucket chain. Null for the last node.
    T * listNext; // Link to next element is the singly-linked list of all nodes. Null for the last node.
    unsigned int hashKey; // Hash value of the node's key. Never zero for a linked node.
};

// ========================================================
// class LinkedHashTable:
//
// The intrusive hash-table template used to order
// Commands and CVars by name. Elements must inherit
// from HashTableLink to be inserted into this data
// structure. It doesn't own the nodes, so the table
// won't try to delete the objects upon destruction.
// The size of the table in buckets is fixed and never
// changes once constructed. We supply a reasonable
// default (DefaultHTSizeHint) that should be enough for
// most applications. The only issue of getting close
// to the hash-table max size is that item collisions
// increase, but the structure should continue to work
// just fine. Key type is fixed to C-style char* string.
// ========================================================

template<class T, class HF>
class LinkedHashTable CFG_FINAL_CLASS
{
    CFG_DISABLE_COPY_ASSIGN(LinkedHashTable);

public:

    // Default value: A prime number close to 1024.
    // If redefining this, make sure to keep it a prime
    // number to ensure a lower number of hash collisions.
    // Larger values should improve lookup speed, but will
    // use more memory (each table entry is the size of a pointer).
    static const int DefaultHTSizeHint = 1033;

    // User provided hash functor.
    // Takes a string and returns an unsigned integer.
    // See the above StringHasher/StringHasherNoCase.
    typedef HF HashFunc;

    LinkedHashTable()
        : table(CFG_NULL)
        , listHead(CFG_NULL)
        , bucketCount(0)
        , usedBuckets(0)
    {
        // Allocates the table on the first insertion.
    }

    ~LinkedHashTable()
    {
        deallocate();
    }

    void allocate(const int sizeInBuckets = DefaultHTSizeHint)
    {
        if (table != CFG_NULL)
        {
            return; // Already allocated.
        }

        table = memAlloc<T *>(sizeInBuckets);
        bucketCount = sizeInBuckets;

        CFG_CLEAR_ARRAY_SIZED(table, sizeInBuckets);
    }

    void deallocate()
    {
        memFree(table);
        table       = CFG_NULL;
        listHead    = CFG_NULL;
        bucketCount = 0;
        usedBuckets = 0;
    }

    T * findByKey(const char * key) const
    {
        CFG_ASSERT(key != CFG_NULL);
        if (isEmpty())
        {
            return CFG_NULL;
        }

        const unsigned int hashKey = hashOf(key);
        for (T * node = table[hashKey % bucketCount]; node; node = node->hashNext)
        {
            if (hashKey == node->hashKey)
            {
                return node;
            }
        }

        return CFG_NULL; // Not found.
    }

    void linkWithKey(T * node, const char * key)
    {
        CFG_ASSERT(key  != CFG_NULL);
        CFG_ASSERT(node != CFG_NULL);
        CFG_ASSERT(node->hashKey == 0); // Can't be linked more than once!

        allocate(); // Ensure buckets are allocated if this is the first use.

        const unsigned int hashKey = hashOf(key);
        const unsigned int bucket  = hashKey % bucketCount;

        // Make the new head node of its chain:
        node->hashKey  = hashKey;
        node->hashNext = table[bucket];
        table[bucket]  = node;

        // Prepend to the sequential chain of all nodes:
        node->listNext = listHead;
        listHead = node;

        ++usedBuckets;
    }

    T * unlinkByKey(const char * key)
    {
        CFG_ASSERT(key != CFG_NULL);
        if (isEmpty())
        {
            return CFG_NULL;
        }

        const unsigned int hashKey = hashOf(key);
        const unsigned int bucket  = hashKey % bucketCount;

        T * previous = CFG_NULL;
        for (T * node = table[bucket]; node;)
        {
            if (hashKey == node->hashKey)
            {
                if (previous != CFG_NULL)
                {
                    // Not the head of the chain, remove from middle:
                    previous->hashNext = node->hashNext;
                }
                else if (node == table[bucket] && node->hashNext == CFG_NULL)
                {
                    // Single node bucket, clear the entry:
                    table[bucket] = CFG_NULL;
                }
                else if (node == table[bucket] && node->hashNext != CFG_NULL)
                {
                    // Head of chain with other node(s) following:
                    table[bucket] = node->hashNext;
                }
                else
                {
                    CFG_ASSERT(false && "LinkedHashTable bucket chain is corrupted!");
                }

                node->hashNext = CFG_NULL;
                node->hashKey  = 0;
                unlinkFromList(node);
                --usedBuckets;
                return node;
            }

            previous = node;
            node = node->hashNext;
        }

        return CFG_NULL; // No such node with the given key.
    }

    // Misc accessors:
    T *  getFirst() const { return listHead; }
    int  getSize()  const { return usedBuckets; }
    bool isEmpty()  const { return usedBuckets == 0; }

private:

    unsigned int hashOf(const char * key) const
    {
        const unsigned int hashKey = HashFunc()(key);
        CFG_ASSERT(hashKey != 0 && "Null hash indexes not allowed!");
        return hashKey;
    }

    void unlinkFromList(T * node)
    {
        if (node == listHead) // Easy case, remove the fist node.
        {
            listHead = listHead->listNext;
            node->listNext = CFG_NULL;
            return;
        }

        T * curr = listHead;
        T * prev = CFG_NULL;

        // Find the element before the node to remove:
        while (curr != node)
        {
            prev = curr;
            curr = curr->listNext;
        }

        // Should have found the node somewhere...
        CFG_ASSERT(curr != CFG_NULL);
        CFG_ASSERT(prev != CFG_NULL);

        // Unlink the node by pointing its previous element to what was after it:
        prev->listNext = node->listNext;
        node->listNext = CFG_NULL;
    }

    // Array of pointers to items (the buckets).
    T ** table;

    // First node in the linked list of all table nodes.
    // This will point to the most recently inserted item.
    T * listHead;

    // Total size in buckets (items) of `table[]` and buckets used so far.
    int bucketCount;
    int usedBuckets;
};

// ========================================================
// class SmallStr:
//
// Simple dynamically sized string class with SSO - Small
// String Optimization for strings under 40 characters.
// A small buffer of chars is kept inline with the object
// to avoid a dynamic memory alloc for small strings.
// It can also grow to accommodate arbitrarily sized strings.
// The overall design is somewhat similar to std::string.
// Total structure size should be ~48 bytes.
// ========================================================

//TODO make non-inline
class SmallStr CFG_FINAL_CLASS
{
public:

    SmallStr()
    {
        initInternal(CFG_NULL, 0);
    }

    ~SmallStr()
    {
        if (isDynamic())
        {
            memFree(backingStore.dynamic);
        }
        // else storage is inline with the object.
    }

    SmallStr(const char * str)
    {
        CFG_ASSERT(str != CFG_NULL);
        initInternal(str, CFG_ISTRLEN(str));
    }

    SmallStr(const char * str, const int len)
    {
        CFG_ASSERT(str != CFG_NULL);
        initInternal(str, len);
    }

    SmallStr(const SmallStr & other)
    {
        initInternal(other.getCString(), other.getLength());
    }

    SmallStr & operator = (const SmallStr & other)
    {
        setCString(other.getCString(), other.getLength());
        return *this;
    }

    SmallStr & operator = (const char * str)
    {
        setCString(str);
        return *this;
    }

    void setCString(const char * str)
    {
        CFG_ASSERT(str != CFG_NULL);
        setCString(str, CFG_ISTRLEN(str));
    }

    void setCString(const char * str, const int len)
    {
        CFG_ASSERT(str != CFG_NULL);

        if (len <= 0 || *str == '\0')
        {
            clear();
            return;
        }
        if ((len + 1) > capacity)
        {
            resizeInternal(len + 1);
        }

        copyString(getCString(), capacity, str);
        length = len;
    }

    char * getCString()
    {
        return (!isDynamic() ? backingStore.fixed : backingStore.dynamic);
    }

    const char * getCString() const
    {
        return (!isDynamic() ? backingStore.fixed : backingStore.dynamic);
    }

    bool isDynamic()  const { return capacity > static_cast<int>(sizeof(backingStore)); }
    bool isEmpty()    const { return length == 0; }

    int getLength()   const { return length;   }
    int getCapacity() const { return capacity; }

    // c_str() method is for compatibility with std::string.
    const char * c_str() const { return getCString(); }

    char & operator[] (const int index)
    {
        CFG_ASSERT(index >= 0 && index < length);
        return getCString()[index];
    }

    char operator[] (const int index) const
    {
        CFG_ASSERT(index >= 0 && index < length);
        return getCString()[index];
    }

    void clear()
    {
        // Does not free dynamic memory.
        length = 0;
        getCString()[0] = '\0';
    }

    //
    // Swap the internal contents of two SmallStrs:
    //

    friend void swap(SmallStr & first, SmallStr & second)
    {
        using std::swap;

        if (first.isDynamic() && second.isDynamic())
        {
            swap(first.length,   second.length);
            swap(first.capacity, second.capacity);
            swap(first.backingStore.dynamic, second.backingStore.dynamic);
        }
        else // Storage is inline with one/both the objects, need to strcpy.
        {
            SmallStr temp(first);
            first  = second;
            second = temp;
        }
    }

    //
    // Compare against other SmallStr or a char* string:
    //

    bool operator == (const SmallStr & other) const
    {
        return std::strcmp(getCString(), other.getCString()) == 0;
    }
    bool operator != (const SmallStr & other) const
    {
        return std::strcmp(getCString(), other.getCString()) != 0;
    }

    bool operator == (const char * str) const
    {
        CFG_ASSERT(str != CFG_NULL);
        return std::strcmp(getCString(), str) == 0;
    }
    bool operator != (const char * str) const
    {
        CFG_ASSERT(str != CFG_NULL);
        return std::strcmp(getCString(), str) != 0;
    }

private:

    void initInternal(const char * str, const int len)
    {
        length   = 0;
        capacity = sizeof(backingStore);
        std::memset(&backingStore, 0, sizeof(backingStore));

        if (str != CFG_NULL)
        {
            setCString(str, len);
        }
    }

    void resizeInternal(int newCapacity)
    {
        const bool isDyn = isDynamic();

        // This many extra chars are added to the new capacity request to
        // avoid more allocations if the string grows again in the future.
        // This can be tuned for environments with more limited memory.
        newCapacity += 64;
        char * mem = memAlloc<char>(newCapacity);

        // Notice that we don't bother copying the old string.
        // This is intentional since this method is only called
        // from setCString().
        if (isDyn)
        {
            memFree(backingStore.dynamic);
        }

        capacity = newCapacity;
        backingStore.dynamic = mem;
    }

    int length;   // Chars used in string, not counting a '\0' at the end.
    int capacity; // Total chars available for use.

    // Either we are using the small fixed-size buffer
    // or the 'dynamic' field which is heap allocated.
    // Dynamic memory is in use if capacity > sizeof(backingStore)
    union
    {
        char * dynamic;
        char fixed[40];
    } backingStore;
};

} // namespace cfg {}

#endif // CFG_UTILS_HPP
