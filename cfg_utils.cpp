
// ================================================================================================
// -*- C++ -*-
// File: cfg_utils.cpp
// Author: Guilherme R. Lampert
// Created on: 27/11/15
// Brief: Misc utilities and helpers for the CFG library. This is an use internal file.
// ================================================================================================

#include "cfg_utils.hpp"

namespace cfg
{

// Controls the out of CFG_ERROR().
bool silentErrors = false;

// ========================================================
// Boolean value strings for CVarStr:
// ========================================================

static const BoolCStr defaultBoolStrings[] =
{
    { "true"  , "false"  },
    { "yes"   , "no"     },
    { "on"    , "off"    },
    { "1"     , "0"      },
    { CFG_NULL, CFG_NULL }
};

static const BoolCStr * currentBoolStrings = defaultBoolStrings;

const BoolCStr * getBoolStrings()
{
    return currentBoolStrings;
}

void setBoolStrings(const BoolCStr * strings)
{
    if (strings == CFG_NULL) // Use null to restore defaults.
    {
        currentBoolStrings = defaultBoolStrings;
    }
    else
    {
        currentBoolStrings = strings;
    }
}

// ========================================================
// Common utility functions:
// ========================================================

char * cloneString(const char * src)
{
    CFG_ASSERT(src != CFG_NULL);

    const int len = CFG_ISTRLEN(src) + 1;
    char * newString = memAlloc<char>(len);
    copyString(newString, len, src);

    return newString;
}

int copyString(char * dest, int destSizeInChars, const char * source)
{
    CFG_ASSERT(dest != CFG_NULL);
    CFG_ASSERT(source != CFG_NULL);
    CFG_ASSERT(destSizeInChars > 0);

    // Copy until the end of source or until we run out of space in dest:
    char * ptr = dest;
    while ((*ptr++ = *source++) && --destSizeInChars > 0) { }

    // Truncate on overflow:
    if (destSizeInChars == 0)
    {
        *(--ptr) = '\0';
        CFG_ERROR("Overflow in copyString()! Output was truncated.");
        return static_cast<int>(ptr - dest);
    }

    // Return the number of chars written to dest (not counting the null terminator).
    return static_cast<int>(ptr - dest - 1);
}

int compareStringsNoCase(const char * s1, const char * s2, unsigned int count)
{
    CFG_ASSERT(s1 != CFG_NULL);
    CFG_ASSERT(s2 != CFG_NULL);

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

char * rightTrimString(char * strIn)
{
    CFG_ASSERT(strIn != CFG_NULL);
    if (*strIn == '\0')
    {
        return strIn;
    }

    int length = CFG_ISTRLEN(strIn);
    char * ptr = strIn + length - 1;

    while (length > 0 && isWhitespace(*ptr))
    {
        *ptr-- = '\0', length--;
    }

    return strIn;
}

} // namespace cfg {}
