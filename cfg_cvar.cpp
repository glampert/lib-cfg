
// ================================================================================================
// -*- C++ -*-
// File: cfg_cvar.cpp
// Author: Guilherme R. Lampert
// Created on: 09/11/15
// Brief: Implementation of the CVar System for the CFG library.
// ================================================================================================

#include "cfg_cvar.hpp"

namespace cfg
{
namespace detail
{

// ========================================================
// Local helper functions:
// ========================================================

const char ** cloneStringArray(const char ** strings)
{
    if (strings == CFG_NULL)
    {
        return CFG_NULL;
    }

    int i;
    char * str;
    const char ** ptr;
    int totalLength = 0;

    for (i = 0; strings[i] != CFG_NULL; ++i)
    {
        totalLength += CFG_ISTRLEN(strings[i]) + 1;
    }

    // Allocate everything (pointers + strings) as a single memory chunk:
    const std::size_t pointerBytes = (i + 1) * sizeof(char *);
    ptr = reinterpret_cast<const char **>(memAlloc<UByte>(pointerBytes + totalLength));
    str = reinterpret_cast<char *>(reinterpret_cast<UByte *>(ptr) + pointerBytes);

    for (i = 0; strings[i] != CFG_NULL; ++i)
    {
        ptr[i] = str;
        std::strcpy(str, strings[i]);
        str += CFG_ISTRLEN(strings[i]) + 1;
    }

    // Array is null terminated.
    ptr[i] = CFG_NULL;
    return ptr;
}

bool intToString(unsigned int number, char * dest, const int destSizeInChars, const int numBase, const bool isNegative)
{
    CFG_ASSERT(dest != CFG_NULL);
    CFG_ASSERT(destSizeInChars > 3); // - or 0x and a '\0'

    // Supports binary, octal, decimal and hexadecimal.
    const bool goodBase = (numBase == 2  || numBase == 8 ||
                           numBase == 10 || numBase == 16);
    if (!goodBase)
    {
        CFG_ERROR("Bad numeric base (%d)!", numBase);
        dest[0] = '\0';
        return false;
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
            number = static_cast<unsigned int>(-static_cast<int>(number));
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
    }
    while (number > 0 && length < destSizeInChars);

    // Check for buffer overflow. Return an empty string in such case.
    if (length >= destSizeInChars)
    {
        CFG_ERROR("Buffer overflow in integer => string conversion!");
        dest[0] = '\0';
        return false;
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
    }
    while (firstDigit < ptr);

    // Converted successfully.
    return true;
}

int trimTrailingZeros(char * str)
{
    CFG_ASSERT(str != CFG_NULL);

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

bool cvarNameStartsWith(const char * name, const char * prefix)
{
    CFG_ASSERT(name   != CFG_NULL);
    CFG_ASSERT(prefix != CFG_NULL);

    const int nameLen   = CFG_ISTRLEN(name);
    const int prefixLen = CFG_ISTRLEN(prefix);

    if (nameLen < prefixLen)
    {
        return false;
    }
    if (nameLen == 0 || prefixLen == 0)
    {
        return false;
    }

    #if CFG_CVAR_CASE_SENSITIVE_NAMES
    return std::strncmp(name, prefix, prefixLen) == 0;
    #else // !CFG_CVAR_CASE_SENSITIVE_NAMES
    return compareStringsNoCase(name, prefix, prefixLen) == 0;
    #endif // CFG_CVAR_CASE_SENSITIVE_NAMES
}

bool cvarSortPredicate(const CVarInterface * a, const CVarInterface * b)
{
    return cvarCompNames(a->getName(), b->getName()) < 0;
}

void cvarSortMatches(CVarInterface ** matches, const int count)
{
    std::sort(matches, matches + count, cvarSortPredicate);
}

// ========================================================
// class CVarInterface (internal):
// ========================================================

CVarInterface::CVarInterface(const char * varName, const char * varDesc, const CVarFlags varFlags)
    : flags(varFlags)
    , name(CFG_NULL)
    , desc(CFG_NULL)
{
    // Must have a valid name!
    CFG_ASSERT(varName != CFG_NULL && *varName != '\0');
    name = cloneString(varName);

    if (varDesc != CFG_NULL && *varDesc != '\0')
    {
        desc = cloneString(varDesc);
    }
}

CVarInterface::~CVarInterface()
{
    memFree(name);
    memFree(desc);
}

bool CVarInterface::equals(const CVarInterface & other) const
{
    if (getTypeCategory() != other.getTypeCategory())
    {
        return false;
    }
    if (getFlags() != other.getFlags())
    {
        return false;
    }
    if (cvarCompNames(getName(), other.getName()) != 0)
    {
        return false;
    }
    // Description is not compared on purpose. It's just metadata for displaying.
    return true;
}

// ========================================================
// class CVarImplStr (internal):
// ========================================================

CVarImplStr::CVarImplStr(const char * varName,
                         const char * varValue,
                         const char * varDesc,
                         const CVarFlags varFlags,
                         const char ** allowedValues)
    : CVarInterface(varName, varDesc, varFlags)
    , numFormat(static_cast<UByte>(Decimal))
    , updateInt(true)
    , updateFloat(true)
    , intValue(0)
    , floatValue(0)
    , stringValues(CFG_NULL)
    , currentValue(varValue)
    , defaultValue(varValue)
{
    if (allowedValues != CFG_NULL)
    {
        // Enforce `RangeCheck` if we have stringValues.
        setFlags(getFlags() | RangeCheck);
        stringValues = cloneStringArray(allowedValues);
    }
    else
    {
        // Make sure `RangeCheck` is cleared. Nothing to check against...
        setFlags(getFlags() & ~RangeCheck);
    }
}

CVarImplStr::CVarImplStr(const CVarImplStr & other)
    : CVarInterface(other.getName(), other.getDesc(), other.getFlags())
    , numFormat(other.numFormat)
    , updateInt(other.updateInt)
    , updateFloat(other.updateFloat)
    , intValue(other.intValue)
    , floatValue(other.floatValue)
    , stringValues(cloneStringArray(other.stringValues))
    , currentValue(other.currentValue)
    , defaultValue(other.defaultValue)
{
}

CVarImplStr::~CVarImplStr()
{
    memFree(stringValues);
}

CVarImplStr & CVarImplStr::operator = (const CVarImplStr & other)
{
    trySetValueInternal(other.currentValue.getCString(),
                        other.currentValue.getLength());
    return *this;
}

CVarImplStr & CVarImplStr::operator = (const char * str)
{
    CFG_ASSERT(str != CFG_NULL);
    trySetValueInternal(str, CFG_ISTRLEN(str));
    return *this;
}

const char ** CVarImplStr::getAllowedValues() const
{
    return stringValues;
}

bool CVarImplStr::isValueAllowed(const char * str) const
{
    CFG_ASSERT(str != CFG_NULL);

    // Anything goes?
    if (stringValues == CFG_NULL || !(getFlags() & RangeCheck))
    {
        return true;
    }

    // Check against each possible value:
    for (int i = 0; stringValues[i] != CFG_NULL; ++i)
    {
        if (cvarCompStrings(stringValues[i], str) == 0)
        {
            return true;
        }
    }
    return false;
}

int CVarImplStr::scanFloats(float * valuesOut, const int maxValues) const
{
    CFG_ASSERT(valuesOut != CFG_NULL);
    CFG_ASSERT(maxValues > 0);

    //
    // The scanned values may be enclosed between the accepted delimiters,
    // example: "(1.1, 2.2, 3.3), [...] or even {...}"
    // Any number of whitespace permitted in between.
    //
    const char * delimiters = "()[]{}, \t\n\r";
    int valuesScanned = 0;

    for (const char * valPtr = currentValue.getCString(); *valPtr != '\0';)
    {
        // Find beginning of a potential number/token:
        const char * numStart = CFG_NULL;
        for (const char * dl = delimiters; *dl != '\0'; ++dl)
        {
            if (*valPtr == *dl)
            {
                numStart = valPtr + 1;
                break;
            }
        }

        if (numStart == CFG_NULL || *numStart == '\0')
        {
            break; // Done.
        }

        char * endPtr = CFG_NULL;
        const double num = std::strtod(numStart, &endPtr);
        if (endPtr == CFG_NULL || endPtr == numStart)
        {
            ++valPtr;
            continue; // Not a number, apparently...
        }

        valuesOut[valuesScanned++] = static_cast<float>(num);
        if (valuesScanned == maxValues)
        {
            break;
        }

        valPtr = endPtr;
    }

    return valuesScanned;
}

bool CVarImplStr::equals(const CVarInterface & other) const
{
    if (!CVarInterface::equals(other))
    {
        return false;
    }

    // The base equals() should have failed for different types,
    // so this cast is safe. Nevertheless, assert to be sure.
    CFG_ASSERT(getTypeCategory() == other.getTypeCategory());
    const CVarImplStr & var = static_cast<const CVarImplStr &>(other);

    // Compare all properties:
    if (numFormat != var.numFormat)
    {
        return false;
    }
    if (currentValue != var.currentValue)
    {
        return false;
    }
    if (defaultValue != var.defaultValue)
    {
        return false;
    }

    // Test the string value if everything else matched.
    if (stringValues == CFG_NULL && var.stringValues == CFG_NULL)
    {
        return true;
    }
    if (stringValues != CFG_NULL && var.stringValues == CFG_NULL)
    {
        return false;
    }
    if (stringValues == CFG_NULL && var.stringValues != CFG_NULL)
    {
        return false;
    }

    for (int i = 0; stringValues[i] != CFG_NULL; ++i)
    {
        if (var.stringValues[i] == CFG_NULL)
        {
            return false;
        }
        if (cvarCompStrings(stringValues[i], var.stringValues[i]) != 0)
        {
            return false;
        }
    }

    return true; // One and the same!
}

void CVarImplStr::setNumberFormatting(const NumFormat format)
{
    numFormat = static_cast<UByte>(format);
}

bool CVarImplStr::setDefault()
{
    return trySetValueInternal(defaultValue.getCString(),
                               defaultValue.getLength());
}

const char * CVarImplStr::getTypeString() const
{
    return "string";
}

CVarImplStr::TypeCategory CVarImplStr::getTypeCategory() const
{
    return TypeString;
}

const char * CVarImplStr::getCString() const
{
    return currentValue.getCString();
}

bool CVarImplStr::setCString(const char * value)
{
    CFG_ASSERT(value != CFG_NULL);
    return trySetValueInternal(value, CFG_ISTRLEN(value));
}

bool CVarImplStr::getBool() const
{
    const BoolCStr * bStrings = getBoolStrings();
    const char * currVal = currentValue.getCString();

    for (int i = 0; bStrings[i].trueStr != CFG_NULL; ++i)
    {
        if (cvarCompStrings(bStrings[i].trueStr, currVal) == 0)
        {
            return true;
        }
        if (cvarCompStrings(bStrings[i].falseStr, currVal) == 0)
        {
            return false;
        }
    }

    CFG_ERROR("No available conversion from '%s' to boolean!", getName());
    return false; // The default. Not distinguishable from a 'correct'
                  // return, but at least we've logged the error...
}

bool CVarImplStr::setBool(const bool value)
{
    // For setting the value, the first bool string is always used.
    const BoolCStr * bStrings = getBoolStrings();
    const char * str = (value ? bStrings[0].trueStr : bStrings[0].falseStr);
    return trySetValueInternal(str, CFG_ISTRLEN(str));
}

int CVarImplStr::getInt() const
{
    if (updateInt) // Updated only when needed.
    {
        char * endPtr     = CFG_NULL;
        const char * nPtr = currentValue.getCString();
        const long num    = std::strtol(nPtr, &endPtr, 0);

        if (endPtr == CFG_NULL || endPtr == nPtr)
        {
            CFG_ERROR("No available conversion from '%s' to integer!", getName());
            return 0;
        }

        intValue  = static_cast<int>(num);
        updateInt = false;
    }
    return intValue;
}

bool CVarImplStr::setInt(const int value)
{
    char numStr[TempStrMaxSize];
    const bool ok = intToString(static_cast<unsigned int>(value), numStr,
                                CFG_ARRAY_LEN(numStr), numFormat, (value < 0));
    if (!ok)
    {
        CFG_ERROR("Can't set string CVar '%s' from integer value %d!",
                  getName(), value);
        return false;
    }

    return trySetValueInternal(numStr, CFG_ISTRLEN(numStr));
}

unsigned int CVarImplStr::getUInt() const
{
    return static_cast<unsigned int>(getInt());
}

bool CVarImplStr::setUInt(const unsigned int value)
{
    return setInt(static_cast<int>(value));
}

float CVarImplStr::getFloat() const
{
    return static_cast<float>(getDouble());
}

bool CVarImplStr::setFloat(const float value)
{
    return setDouble(value);
}

double CVarImplStr::getDouble() const
{
    if (updateFloat) // Updated only when needed.
    {
        char * endPtr     = CFG_NULL;
        const char * nPtr = currentValue.getCString();
        const double num  = std::strtod(nPtr, &endPtr);

        if (endPtr == CFG_NULL || endPtr == nPtr)
        {
            CFG_ERROR("No available conversion from '%s' to floating-point!", getName());
            return 0;
        }

        floatValue  = num;
        updateFloat = false;
    }
    return floatValue;
}

bool CVarImplStr::setDouble(const double value)
{
    char numStr[TempStrMaxSize];
    std::snprintf(numStr, sizeof(numStr), "%f", value);
    const int length = trimTrailingZeros(numStr);
    return trySetValueInternal(numStr, length);
}

bool CVarImplStr::trySetValueInternal(const char * str, const int len)
{
    CFG_ASSERT(str != CFG_NULL);

    if (!isWritable())
    {
        CFG_ERROR("CVar '%s' is read-only!", getName());
        return false;
    }

    if (!isValueAllowed(str))
    {
        CFG_ERROR("String value \"%s\" not allowed for '%s'!", str, getName());
        return false;
    }

    // Invalidate the cached values.
    // Next time the user queries them they'll be updated.
    updateInt   = true;
    updateFloat = true;
    intValue    = 0;
    floatValue  = 0;

    currentValue.setCString(str, len);
    setModified();

    return true;
}

} // namespace detail {}

// ========================================================
// class CVarManager:
// ========================================================

CVarManager::CVarManager()
{
}

CVarManager::CVarManager(const int hashTableSize)
{
    if (hashTableSize > 0)
    {
        registeredCVars.allocate(hashTableSize);
    }
}

CVarManager::~CVarManager()
{
    // Cleanup the memory we own (just the non-External instances)
    CVar * cvar = registeredCVars.getFirst();
    while (cvar != CFG_NULL)
    {
        CVar * temp = cvar->getNext();
        if (!(cvar->getFlags() & CVar::External))
        {
            CFG_DELETE cvar;
        }
        cvar = temp;
    }
}

CVar * CVarManager::findCVar(const char * name) const
{
    CFG_ASSERT(name != CFG_NULL);
    if (*name == '\0')
    {
        return CFG_NULL;
    }
    return registeredCVars.findByKey(name);
}

int CVarManager::findCVarsWithPartialName(const char * partialName, CVar ** matches, const int maxMatches) const
{
    CFG_ASSERT(partialName != CFG_NULL);
    CFG_ASSERT(matches != CFG_NULL);
    CFG_ASSERT(maxMatches > 0);

    if (*partialName == '\0')
    {
        return 0;
    }

    // Partial name searching unfortunately can't take advantage of the optimized
    // constant time hash-table lookup, so we have to fall back to a linear search.
    int matchesFound = 0;
    for (CVar * cvar = registeredCVars.getFirst();
         cvar != CFG_NULL; cvar = cvar->getNext())
    {
        if (detail::cvarNameStartsWith(cvar->getName(), partialName))
        {
            if (matchesFound < maxMatches)
            {
                matches[matchesFound] = cvar;
            }
            ++matchesFound; // Keep incrementing even if matches[] is full,
                            // so the caller can know the total found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        detail::cvarSortMatches(matches, std::min(matchesFound, maxMatches));
    }

    return matchesFound;
}

int CVarManager::findCVarsWithFlags(const CVarFlags flags, CVar ** matches, const int maxMatches) const
{
    CFG_ASSERT(matches != CFG_NULL);
    CFG_ASSERT(maxMatches > 0);

    int matchesFound = 0;
    for (CVar * cvar = registeredCVars.getFirst();
         cvar != CFG_NULL; cvar = cvar->getNext())
    {
        if (cvar->getFlags() & flags)
        {
            if (matchesFound < maxMatches)
            {
                matches[matchesFound] = cvar;
            }
            ++matchesFound; // Keep incrementing even if matches[] is full,
                            // so the caller can know the total found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        detail::cvarSortMatches(matches, std::min(matchesFound, maxMatches));
    }

    return matchesFound;
}

bool CVarManager::registerCVarPreValidate(const char * name, const CVar * newVar) const
{
    if (!isValidCVarName(name))
    {
        CFG_ERROR("Bad CVar name '%s'! Can't register it.", name);
        return false;
    }

    if (const CVar * cvar = findCVar(name))
    {
        if (newVar != CFG_NULL)
        {
            if (newVar->equals(*cvar))
            {
                CFG_ERROR("CVar '%s' already registered and with the same properties of existing one!", name);
            }
            else
            {
                CFG_ERROR("CVar '%s' already registered and properties differ from existing one!", name);
            }
        }
        else
        {
            CFG_ERROR("CVar '%s' already registered! Duplicate names are not allowed.", name);
        }
        return false;
    }

    return true;
}

bool CVarManager::registerCVar(CVar * newVar)
{
    CFG_ASSERT(newVar != CFG_NULL);

    const char * name = newVar->getName();
    if (!registerCVarPreValidate(name, newVar))
    {
        return false;
    }

    registeredCVars.linkWithKey(newVar, name);
    return true;
}

bool CVarManager::removeCVar(const char * name)
{
    if (!isValidCVarName(name))
    {
        CFG_ERROR("'%s' is not a valid CVar name! Nothing to remove.", name);
        return false;
    }

    CVar * cvar = registeredCVars.unlinkByKey(name);
    if (cvar == CFG_NULL)
    {
        return false; // No such variable.
    }

    // Free the memory if we own it (it's a dynamic variable).
    if (!(cvar->getFlags() & CVar::External))
    {
        CFG_DELETE cvar;
    }

    return true;
}

bool CVarManager::removeCVar(CVar * cvar)
{
    CFG_ASSERT(cvar != CFG_NULL);
    return removeCVar(cvar->getName());
}

int CVarManager::getRegisteredCount() const
{
    return registeredCVars.getSize();
}

bool CVarManager::isValidCVarName(const char * name)
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

    if (name == CFG_NULL || *name == '\0')
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
            // If it is a dot or an underscore its OK, otherwise invalid.
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

} // namespace cfg {}
