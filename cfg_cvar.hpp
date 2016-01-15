
// ================================================================================================
// -*- C++ -*-
// File: cfg_cvar.hpp
// Author: Guilherme R. Lampert
// Created on: 09/11/15
// Brief: Declarations for the CVar classes, CVarManager and related interfaces.
// ================================================================================================

#ifndef CFG_CVAR_HPP
#define CFG_CVAR_HPP

#include "cfg_utils.hpp"
#include <cstdio> // For std::snprintf

#if CFG_CVAR_WANTS_STD_STRING_CONVERSION
#include <string>
#endif // CFG_CVAR_WANTS_STD_STRING_CONVERSION

namespace cfg
{

// Integer type for miscellaneous CVar bitflags.
// Not to be confused with one of the following CVar types!
typedef int CVarFlags;

// This namespace wraps internal details of the CVar System that have
// to be declared inline (a couple template classes and functions).
// The end-user of the module should not interact with this namespace
// directly, since the types and functions defined within are not
// guaranteed to keep the same interfaces over time.
namespace detail
{

// Shorthand alias for an unsigned byte [0,255] integer type.
typedef unsigned char UByte;

// Size of small stack-declared strings for things like number => string conversions.
static const int TempStrMaxSize = 128;

// ========================================================
// class CVarInterface (internal):
// ========================================================

class CVarInterface
    : public HashTableLink<CVarInterface>
{
public:

// TODO
// - need to somehow be able to enumerate the allowed values (min/max for the num vars)
// - need to decide on the value completion callback
// - provide a onValueModified callback??? Probably interesting to allow extending things like the 'set' commands...

    // Type category flags:
    enum TypeCategory
    {
        TypeNumber,
        TypeString
    };

    // Predefined CVar flags (user can extend these):
    enum BaseFlags
    {
        External   = 1 << 0, // If set, doesn't belong to the CVarManager and so is not deleted when unregistered.
        Modified   = 1 << 1, // Marked as modified to eventually be synced with its permanent storage.
        Persistent = 1 << 2, // Value persists after program termination and is restored in a future restart.
        Volatile   = 1 << 3, // Value persists only while the program is running. Mutually exclusive with `Persistent`.
        ReadOnly   = 1 << 4, // For display only. Cannot be changed via command-line/cfg-files or the set*() methods.
        InitOnly   = 1 << 5, // Similar to `ReadOnly` but can be set by the startup command line of the app.
        RangeCheck = 1 << 6  // Enforces min/max bounds for number vars and allowed strings for string vars.
    };

    // Number formatting/representations.
    // Use these with `setNumberFormatting()` to define how number CVar are converted
    // to strings and how string CVars are rendered when initialized from numbers.
    enum NumFormat
    {
        Binary      = 2,
        Octal       = 8,
        Decimal     = 10,
        Hexadecimal = 16
    };

    // Name and description cannot be changed after the CVar is created.
    // Returned pointers belong to the CVar. Never try to free them!
    const char * getName() const { return name; }
    const char * getDesc() const { return (desc != CFG_NULL) ? desc : ""; }

    // User is free to query and change the CVar flags after construction.
    CVarFlags getFlags() const { return flags; }
    void setFlags(const CVarFlags newFlags) { flags = newFlags; }

    // Direct access to the modified flag.
    void setModified()   { flags |=  Modified; }
    void clearModified() { flags &= ~Modified; }
    bool isModified() const { return (flags & Modified); }
    bool isWritable() const { return !(flags & (ReadOnly | InitOnly)); }

    // Does a deep comparison to determine if both instances are equivalent.
    virtual bool equals(const CVarInterface & other) const;

    // Set the formatting used to convert numerical CVar values to
    // printable strings, i.e.: binary, octal, decimal, hexadecimal.
    // Default formatting is DECIMAL.
    virtual void setNumberFormatting(NumFormat format) = 0;

    // Rests the CVar to its default value and flags it as modified.
    // ReadOnly/InitOnly CVars cannot be reset, not even to defaults.
    virtual bool setDefault() = 0;

    // Get a printable string of the underlaying type for displaying.
    // E.g.: "int" for integers, "float" for floats, "string" for strings, etc.
    virtual const char * getTypeString() const = 0;

    // Get the type category of the underlaying wrapped value.
    // Currently either string or number (boolean is considered a number).
    virtual TypeCategory getTypeCategory() const = 0;

    // Converts the underlaying value to a printable string.
    // This always succeeds, regardless of the underlaying CVar
    // type. A valid string conversion is always available.
    // The returned string pointer belongs to the CVar. Don't free it.
    virtual const char * getCString() const = 0;

    // Sets the underlaying value from a string, which might be a number,
    // a boolean ("true", "false", "yes", "no") or plain text for a string CVar.
    // If the conversion from string is not possible CFG_ERROR is called to log
    // the issue and the var is not changed. False is returned in such case.
    virtual bool setCString(const char * value) = 0;

    // Tries to cast to boolean.
    // Returns false if the cast is impossible and logs the error.
    virtual bool getBool() const = 0;

    // Tries to set the underlaying value from a boolean.
    // If the conversion is not possible an error is logged and false is returned.
    virtual bool setBool(bool value) = 0;

    // Tries to cast to integer.
    // Returns zero if the cast is impossible and logs the error.
    virtual int getInt() const = 0;

    // Tries to set the underlaying value from a signed integer.
    // If the conversion is not possible an error is logged and false is returned.
    virtual bool setInt(int value) = 0;

    // Same as the above but overloaded for unsigned integers.
    virtual unsigned int getUInt() const = 0;
    virtual bool setUInt(unsigned int value) = 0;

    // Tries to cast to a floating-point value.
    // Returns 0.0f if the cast is impossible and logs the error.
    virtual float getFloat() const = 0;

    // Sets the underlaying value from a C++ float.
    // If the conversion is not possible an error is logged and false is returned.
    virtual bool setFloat(float value) = 0;

    // Same as the above but overloaded for the double type.
    virtual double getDouble() const = 0;
    virtual bool setDouble(double value) = 0;

    #if CFG_CVAR_WANTS_STD_STRING_CONVERSION
    // Optional std::string interoperability.
    // Same behavior of setCString()/getCString().
    virtual std::string getStdString() const = 0;
    virtual bool setStdString(const std::string & value) = 0;
    #endif // CFG_CVAR_WANTS_STD_STRING_CONVERSION

    // Virtual to ensure CVar instances can be safely deleted via
    // CVarInterface pointers. Also frees the name and desc pointers.
    virtual ~CVarInterface();

protected:

    // Name and desc must be provided on creation, since those are not modifiable later on.
    // Flags can be changed at any time by the user code. Name is the only mandatory non-null
    // non-empty parameter. Flags may be zero and description may be null/empty.
    CVarInterface(const char * varName, const char * varDesc, CVarFlags varFlags);

private:

    // Miscellaneous CVar bitflags, including user defined flags.
    CVarFlags flags;

    // A name must always be provided. Description may be a null string.
    // Allocate them dynamically instead of using fixed size arrays, to
    // keep the CVar objects smaller.
    const char * name;
    const char * desc;
};

// ========================================================
// class CVarImplStr (internal):
//
// CVar class type for text strings. Also optimized for
// strings representing numbers, so conversions between
// number <=> string are cheap. User can supply a list
// of accepted string values on construction.
// ========================================================

//
//TODO
//
// +/+= operators for appending text???
//
class CVarImplStr CFG_FINAL_CLASS
    : public CVarInterface
{
public:

    // Construct with an initial string. This string will be the default reset value (may be empty).
    // Allowed strings is optional. If it is non-null, the `RangeCheck` flag is implied.
    // `allowedValues[]` is an array of C-string pointers terminated by a null pointer.
    explicit CVarImplStr(const char * varName,
                         const char * varValue       = "",
                         const char * varDesc        = "",
                         const CVarFlags varFlags    = 0,
                         const char ** allowedValues = CFG_NULL);

    // Construct from another CVarImplStr. Makes an identical copy of the source.
    CVarImplStr(const CVarImplStr & other);

    // Cleans up the dynamic content.
    ~CVarImplStr();

    // Assign the underlaying value of the other CVar, if this is writable.
    // If not, fails with an error printed to the CFG_ERROR log.
    CVarImplStr & operator = (const CVarImplStr & other);

    // Assign from const char*, overwriting current value if writable.
    CVarImplStr & operator = (const char * str);

    // Get the array of allowed string values.
    // The array is terminated by a null pointer.
    const char ** getAllowedValues() const;

    // Test if the input string value is in the allowed list of strings.
    // Will always succeed if `stringValues[]` is null or if the RangeCheck flag is not set.
    bool isValueAllowed(const char * str) const;

    // Tries to scan a tuple of floats from the current string.
    // Returns the number of floats found. This is handy to parse string
    // CVars that contain a vector of floats or a normalized RGBA color value.
    int scanFloats(float * valuesOut, const int maxValues) const;

    // Explicitly get a pointer to the underlaying char* string.
    const char * get() const { return currentValue.getCString(); }

    // Implicit conversion to a const char* string.
    operator const char *() const { return currentValue.getCString(); }

    //
    // Methods implemented from CVarInterface:
    //

    bool equals(const CVarInterface & other) const CFG_OVERRIDE;

    void setNumberFormatting(NumFormat format) CFG_OVERRIDE;
    bool setDefault() CFG_OVERRIDE;

    const char * getTypeString() const CFG_OVERRIDE;
    TypeCategory getTypeCategory() const CFG_OVERRIDE;

    const char * getCString() const CFG_OVERRIDE;
    bool setCString(const char * value) CFG_OVERRIDE;

    bool getBool() const CFG_OVERRIDE;
    bool setBool(bool value) CFG_OVERRIDE;

    int getInt() const CFG_OVERRIDE;
    bool setInt(int value) CFG_OVERRIDE;

    unsigned int getUInt() const CFG_OVERRIDE;
    bool setUInt(unsigned int value) CFG_OVERRIDE;

    float getFloat() const CFG_OVERRIDE;
    bool setFloat(float value) CFG_OVERRIDE;

    double getDouble() const CFG_OVERRIDE;
    bool setDouble(double value) CFG_OVERRIDE;

    //
    // Optional construct/assign from std::string
    // and convert to std::string implicitly:
    //

    #if CFG_CVAR_WANTS_STD_STRING_CONVERSION

    CVarImplStr & operator = (const std::string & str)
    {
        trySetValueInternal(str.c_str(), static_cast<int>(str.length()));
        return *this;
    }

    operator std::string() const
    {
        return currentValue.getCString();
    }

    std::string getStdString() const CFG_OVERRIDE
    {
        return currentValue.getCString();
    }

    bool setStdString(const std::string & value) CFG_OVERRIDE
    {
        return trySetValueInternal(value.c_str(), static_cast<int>(value.length()));
    }

    #endif // CFG_CVAR_WANTS_STD_STRING_CONVERSION

private:

    bool trySetValueInternal(const char * str, int len);

    // Number formatting for integer conversions.
    // Stored as a byte to save space (can only be 2,8,10,16).
    UByte numFormat;

    // Cached value for quick access. These are lazily initialized
    // only when the values are queried, hence the mutable qualifier.
    mutable bool   updateInt;   // If set `intValue` needs update.
    mutable bool   updateFloat; // If set `floatValue` needs update.
    mutable int    intValue;    // Cached string value as integer.
    mutable double floatValue;  // Cached string value as double.

    // Allowed string values. Last one is a null pointer to signal end of the array.
    // Allocated dynamically on the heap. May be null if any string is to be allowed.
    const char ** stringValues;

    // Current string / default reset string:
    SmallStr currentValue;
    const SmallStr defaultValue;
};

// ========================================================
// More internal helpers:
// ========================================================

int trimTrailingZeros(char * str);
bool intToString(unsigned int number, char * destBuffer, int destSizeInChars, int numBase, bool isNegative);

inline int cvarCompStrings(const char * a, const char * b)
{
    #if CFG_CVAR_CASE_SENSITIVE_STRINGS
    return std::strcmp(a, b);
    #else // !CFG_CVAR_CASE_SENSITIVE_STRINGS
    return compareStringsNoCase(a, b);
    #endif // CFG_CVAR_CASE_SENSITIVE_STRINGS
}

inline int cvarCompNames(const char * a, const char * b)
{
    #if CFG_CVAR_CASE_SENSITIVE_NAMES
    return std::strcmp(a, b);
    #else // !CFG_CVAR_CASE_SENSITIVE_NAMES
    return compareStringsNoCase(a, b);
    #endif // CFG_CVAR_CASE_SENSITIVE_NAMES
}

// ========================================================
// Type-traits-like structures for number-type CVars:
// ========================================================

// Generic unknown type. This is a placeholder;
// Each supported type has a specialization.
template<class T>
struct CVarType
{
    static void toString(T, SmallStr & strOut, int)
    {
        strOut = "???";
    }
    static CFG_CONSTEXPR_FUNC const char * typeString()
    {
        return "???";
    }
};

template<>
struct CVarType<bool>
{
    static void toString(const bool value, SmallStr & strOut, int)
    {
        // For printing, the first boolean string is always the one used.
        const BoolCStr * bStrings = getBoolStrings();
        strOut = (value ? bStrings[0].trueStr : bStrings[0].falseStr);
    }
    static CFG_CONSTEXPR_FUNC const char * typeString() { return "bool"; }
};

template<>
struct CVarType<int>
{
    static void toString(const int value, SmallStr & strOut, const int numFormat)
    {
        char numStr[TempStrMaxSize];
        intToString(static_cast<unsigned int>(value), numStr,
                    CFG_ARRAY_LEN(numStr), numFormat, (value < 0));
        strOut = numStr;
    }
    static CFG_CONSTEXPR_FUNC const char * typeString() { return "int"; }
};

template<>
struct CVarType<unsigned>
{
    static void toString(const unsigned int value, SmallStr & strOut, const int numFormat)
    {
        char numStr[TempStrMaxSize];
        intToString(value, numStr, CFG_ARRAY_LEN(numStr), numFormat, false);
        strOut = numStr;
    }
    static CFG_CONSTEXPR_FUNC const char * typeString() { return "uint"; }
};

template<>
struct CVarType<float>
{
    static void toString(const float value, SmallStr & strOut, int)
    {
        char numStr[TempStrMaxSize];
        std::snprintf(numStr, sizeof(numStr), "%f", value);
        const int length = trimTrailingZeros(numStr);
        strOut.setCString(numStr, length);
    }
    static CFG_CONSTEXPR_FUNC const char * typeString() { return "float"; }
};

template<>
struct CVarType<double>
{
    static void toString(const double value, SmallStr & strOut, int)
    {
        char numStr[TempStrMaxSize];
        std::snprintf(numStr, sizeof(numStr), "%f", value);
        const int length = trimTrailingZeros(numStr);
        strOut.setCString(numStr, length);
    }
    static CFG_CONSTEXPR_FUNC const char * typeString() { return "double"; }
};

// ========================================================
// class CVarImplNum (internal):
//
// CVar type for numbers (integers and floating-point)
// and also booleans (the C++ bool type).
// ========================================================

template<class T>
class CVarImplNum CFG_FINAL_CLASS
    : public CVarInterface
{
public:

    // Default construction sets everything to zero.
    // Reset value will be fixed to zero when calling this constructor.
    explicit CVarImplNum(const char * varName,
                         const char * varDesc     = "",
                         const CVarFlags varFlags = 0)
        : CVarInterface(varName, varDesc, varFlags)
        , currentValue(0)
        , defaultValue(0)
        , minValue(0)
        , maxValue(0)
        , numFormat(static_cast<UByte>(Decimal))
        , updateStrVal(true)
        , strValue()
    {
    }

    // Construct with an initial value that also becomes the default.
    // Min/max value ranges can also be provided but will only be
    // used if the `RangeCheck` flag is also set.
    CVarImplNum(const char * varName,
                const T varValue,
                const char * varDesc     = "",
                const CVarFlags varFlags = 0,
                const T varMin           = 0,
                const T varMax           = 0)
        : CVarInterface(varName, varDesc, varFlags)
        , currentValue(varValue)
        , defaultValue(varValue)
        , minValue(varMin)
        , maxValue(varMax)
        , numFormat(static_cast<UByte>(Decimal))
        , updateStrVal(true)
        , strValue()
    {
    }

    // Construct from another CVarImplNum of the same underlaying type, cloning it.
    CVarImplNum(const CVarImplNum & other)
        : CVarInterface(other.getName(), other.getDesc(), other.getFlags())
        , currentValue(other.currentValue)
        , defaultValue(other.defaultValue)
        , minValue(other.minValue)
        , maxValue(other.maxValue)
        , numFormat(other.numFormat)
        , updateStrVal(other.updateStrVal)
        , strValue(other.strValue)
    {
    }

    // Assign the underlaying value of the other CVar, if this is writable.
    // If not, fails with an error printed to the CFG_ERROR log.
    CVarImplNum & operator = (const CVarImplNum & other)
    {
        trySetValueInternal(other.currentValue);
        return *this;
    }

    // Try to assign compatible raw value.
    // Might fail if this is read-only, in which case the error is logged.
    CVarImplNum & operator = (const T value)
    {
        trySetValueInternal(value);
        return *this;
    }

    // Explicitly get a copy of the underlaying number value.
    T get() const { return currentValue; }

    // Implicit conversion to T, so it behaves
    // more like the underlaying wrapped type.
    operator T() const { return currentValue; }

    //
    // Methods implemented from CVarInterface:
    //

    bool equals(const CVarInterface & other) const CFG_OVERRIDE
    {
        if (!CVarInterface::equals(other))
        {
            return false;
        }

        // The base equals() should have failed for different types,
        // so this cast is safe. Nevertheless, assert to be sure.
        CFG_ASSERT(getTypeCategory() == other.getTypeCategory());
        const CVarImplNum & var = static_cast<const CVarImplNum &>(other);

        // Compare each individual property (strValue is just
        // a cached temp so it is not compared on purpose).
        return (currentValue == var.currentValue &&
                defaultValue == var.defaultValue &&
                minValue     == var.minValue     &&
                maxValue     == var.maxValue     &&
                numFormat    == var.numFormat);
    }

    void setNumberFormatting(const NumFormat format) CFG_OVERRIDE
    {
        numFormat = static_cast<UByte>(format);
    }

    bool setDefault() CFG_OVERRIDE
    {
        return trySetValueInternal(defaultValue);
    }

    const char * getTypeString() const CFG_OVERRIDE
    {
        return CVarType<T>::typeString();
    }

    TypeCategory getTypeCategory() const CFG_OVERRIDE
    {
        return TypeNumber;
    }

    const char * getCString() const CFG_OVERRIDE
    {
        if (updateStrVal)
        {
            CVarType<T>::toString(currentValue, strValue, numFormat);
            updateStrVal = false;
        }
        return strValue.getCString();
    }

    bool setCString(const char * value) CFG_OVERRIDE
    {
        CFG_ASSERT(value != CFG_NULL);

        char * endPtr = CFG_NULL;
        double num = std::strtod(value, &endPtr);

        // Try a boolean if not a number:
        if (endPtr == CFG_NULL || endPtr == value)
        {
            bool bFound = false;
            const BoolCStr * bStrings = getBoolStrings();
            for (int i = 0; bStrings[i].trueStr != CFG_NULL; ++i)
            {
                if (cvarCompStrings(bStrings[i].trueStr, value) == 0)
                {
                    bFound = true; num = 1;
                    break;
                }
                if (cvarCompStrings(bStrings[i].falseStr, value) == 0)
                {
                    bFound = true; num = 0;
                    break;
                }
            }

            if (!bFound)
            {
                CFG_ERROR("Can't set number CVar '%s' from string \"%s\"!", getName(), value);
                return false;
            }
        }

        if (!trySetValueInternal(static_cast<T>(num)))
        {
            return false; // Apparently read-only/init-only.
        }

        // We can take advantage since the input is a string.
        updateStrVal = false;
        strValue.setCString(value);
        return true;
    }

    bool getBool() const CFG_OVERRIDE
    {
        return (currentValue > 0) ? true : false;
    }

    bool setBool(const bool value) CFG_OVERRIDE
    {
        return trySetValueInternal(value ? 1 : 0);
    }

    int getInt() const CFG_OVERRIDE
    {
        return static_cast<int>(currentValue);
    }

    bool setInt(const int value) CFG_OVERRIDE
    {
        return trySetValueInternal(static_cast<T>(value));
    }

    unsigned int getUInt() const CFG_OVERRIDE
    {
        return static_cast<unsigned int>(currentValue);
    }

    bool setUInt(const unsigned int value) CFG_OVERRIDE
    {
        return trySetValueInternal(static_cast<T>(value));
    }

    float getFloat() const CFG_OVERRIDE
    {
        return static_cast<float>(currentValue);
    }

    bool setFloat(const float value) CFG_OVERRIDE
    {
        return trySetValueInternal(static_cast<T>(value));
    }

    double getDouble() const CFG_OVERRIDE
    {
        return static_cast<double>(currentValue);
    }

    bool setDouble(const double value) CFG_OVERRIDE
    {
        return trySetValueInternal(static_cast<T>(value));
    }

    //
    // Optional to/from std::string conversion:
    //

    #if CFG_CVAR_WANTS_STD_STRING_CONVERSION

    std::string getStdString() const CFG_OVERRIDE
    {
        return getCString();
    }

    bool setStdString(const std::string & value) CFG_OVERRIDE
    {
        return setCString(value.c_str());
    }

    #endif // CFG_CVAR_WANTS_STD_STRING_CONVERSION

private:

    bool trySetValueInternal(const T value)
    {
        if (!isWritable())
        {
            CFG_ERROR("CVar '%s' is read-only!", getName());
            return false;
        }

        if (getFlags() & RangeCheck)
        {
            SmallStr valStr, minStr, maxStr;
            if (value < minValue)
            {
                CVarType<T>::toString(value, valStr, numFormat);
                CVarType<T>::toString(minValue, minStr, numFormat);
                CFG_ERROR("Value %s below minimum (%s) for '%s'!",
                          valStr.getCString(), minStr.getCString(), getName());
                return false;
            }
            else if (value > maxValue)
            {
                CVarType<T>::toString(value, valStr, numFormat);
                CVarType<T>::toString(maxValue, maxStr, numFormat);
                CFG_ERROR("Value %s above maximum (%s) for '%s'!",
                          valStr.getCString(), maxStr.getCString(), getName());
                return false;
            }
        }

        currentValue = value;
        updateStrVal = true;
        strValue.clear();
        setModified();
        return true;
    }

    // Current value and reset to:
    T currentValue;
    const T defaultValue;

    // Min/max allowed values (inclusive range).
    // Only enforced if the proper bounds checking flag its set.
    const T minValue;
    const T maxValue;

    // Number formatting for integer => string conversions.
    // Stored as a byte to save space (can only be 2,8,10,16).
    UByte numFormat;

    // Number value cached as a string for displaying.
    // Only updated when the underlaying value has changed
    // (updateStrVal is set) and the user queries the value.
    mutable bool updateStrVal;
    mutable SmallStr strValue;
};

} // namespace detail {}

// ========================================================
// Supported CVar types:
// ========================================================

//
//TODO use extern templates on these if we are building with C++11
//
typedef detail::CVarInterface         CVar;       // Base CVar interface. Not directly instantiable.
typedef detail::CVarImplStr           CVarStr;    // Wraps a dynamically sized string (accepts std::string).
typedef detail::CVarImplNum<bool>     CVarBool;   // Wraps a boolean value into a CVar.
typedef detail::CVarImplNum<int>      CVarInt;    // Wraps a signed integer into a CVar.
typedef detail::CVarImplNum<unsigned> CVarUInt;   // Wraps an unsigned integer into a CVar.
typedef detail::CVarImplNum<float>    CVarFloat;  // Wraps a C++ float into a CVar.
typedef detail::CVarImplNum<double>   CVarDouble; // Wraps a C++ double into a CVar.

// ========================================================
// class CVarManager:
// ========================================================

// TODO
//
// CVar name substitutions on commands
// value completion callback?
// value modified callback?
//
// dynamic definitions?
//
class CVarManager CFG_FINAL_CLASS
{
    CFG_DISABLE_COPY_ASSIGN(CVarManager);

public:

    CVarManager();
    explicit CVarManager(int hashTableSize);

    ~CVarManager();

    //
    // CVar registering / querying:
    //

    CVar * findCVar(const char * name) const;

    // returns the total num of matches found, which might be > than maxMatches
    int findCVarsWithPartialName(const char * partialName, CVar ** matches, int maxMatches) const;
    int findCVarsWithFlags(CVarFlags flags, CVar ** matches, int maxMatches) const;

    bool registerCVar(CVar * newVar);

    template<class Func>
    void enumerateAllCVars(Func fn)
    {
        for (CVar * cvar = registeredCVars.getFirst();
             cvar != CFG_NULL; cvar = cvar->getNext())
        {
            fn(*cvar);
        }
    }

    bool removeCVar(const char * name);
    bool removeCVar(CVar * cvar);

    int getRegisteredCount() const;

    // Tests if a string complies with the CVar naming rules.
    // It DOES NOT check if the variable is already registered.
    static bool isValidCVarName(const char * name);

private:

    // Misc helpers:
    bool registerCVarPreValidate(const char * name, const CVar * newVar) const;

    //
    // Hash function used to lookup CVar names in the HT.
    // Can be case-sensitive or not.
    //
    #if CFG_CVAR_CASE_SENSITIVE_NAMES
    typedef StringHasher CVarNameHasher;
    #else // !CFG_CVAR_CASE_SENSITIVE_NAMES
    typedef StringHasherNoCase CVarNameHasher;
    #endif // CFG_CVAR_CASE_SENSITIVE_NAMES

    // All the registered CVars, in a hash-table for fast lookup by name.
    LinkedHashTable<CVar, CVarNameHasher> registeredCVars;
};

} // namespace cfg {}

#endif // CFG_CVAR_HPP
