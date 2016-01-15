
#include "cfg_cmd.hpp"
#include "cfg_cvar.hpp"
using namespace cfg;

#include <iostream>
#include <cstdlib>
#include <cstdio>

// ================================================================================================
//
// REMEMBER STATIC ANALYSIS!
// Clang Address Sanitizer is also an interesting test...
//
// SHOULD ALLOW THE USER:
//
// - Provide a custom memory allocator
//
// - Allow not just static but also dynamically defined CVars.
//
// - Review some of the assertions. A few of them are probably better as CFG_ERRORs ...
//
// ================================================================================================

#if defined(__GNUC__) || defined(__clang__)
#define CFG_PRINTF_FUNC(fmtIndex, varIndex) __attribute__((format(printf, fmtIndex, varIndex)))
#else // !GNU && !Clang
#define CFG_PRINTF_FUNC(fmtIndex, varIndex) /* unimplemented */
#endif // GNU/Clang

struct SpecialKey
{
    enum Enum
    {
        Return,
        Tab,
        Backspace,
        UpArrow,
        DownArrow,
        RightArrow,
        LeftArrow,
        Escape,
        Control
    };

    static const char * toString(const int key)
    {
        static char charStr[2] = {0,0};
        switch (key)
        {
        case Return     : return "Return";
        case Tab        : return "Tab";
        case Backspace  : return "Backspace";
        case UpArrow    : return "UpArrow";
        case DownArrow  : return "DownArrow";
        case RightArrow : return "RightArrow";
        case LeftArrow  : return "LeftArrow";
        case Escape     : return "Escape";
        case Control    : return "Control";
        default :
            charStr[0] = static_cast<char>(key);
            return charStr;
        } // switch (key)
    }
};

//
// TODO work in progress
//
class SimpleCommandTerminal
{
    CFG_DISABLE_COPY_ASSIGN(SimpleCommandTerminal);

public:

    //
    // Common terminal interface:
    //

    // 'newLineMark' is the character(s) printed at the start of every new input line.
    SimpleCommandTerminal(const char * newLineMark = "> ");
    virtual ~SimpleCommandTerminal();

    // Prints an ASCII string to the console.
    virtual void print(const char * text) = 0;

    // Similar to print() but inserts a newline by default.
    virtual void printLn(const char * text) = 0;

    // Printf-style text printing. No default newline inserted.
    // The default implementation provided relies on the `print()` method.
    virtual void printF(const char * fmt, ...) CFG_PRINTF_FUNC(2, 3);

    // Pushes a character into the line input buffer or handles a special key.
    // Returns true if the key event was handled, false if it was ignored.
    virtual bool handleKeyInput(int key, char chr);

    // Clears the terminal's screen.
    // Note: Be sure to call it even if overridden in a child class.
    virtual void clear();

    //
    // Command history for the current session:
    //

    // Get the number of commands in the history.
    int getCommandHistorySize() const;

    // Get a given command for the command history.
    const char * getCommandFromHistory(int index) const;

    // Clear the current history for this session. Save file unaffected.
    void clearCommandHistory();

    // Prints a list of all commands in the current history using the printF() method.
    void printCommandHistory();

    // Save/load history from file. Save file name is the 'CFG_CMDHIST_FILE' define.
    bool saveCommandHistory();
    bool loadCommandHistory();

    //
    // Exit handlers:
    //

    // Returns true if the interactive terminal should exit/shutdown.
    // This will be set by the built-in "exit" command to allow closing
    // the terminal, since we usually override the traditional CTRL+c
    // interrupt. When the built-in "exit" command is run, it will always
    // fire the onExit() callback, where the terminal impl may cancel the
    // command by calling cancelExit().
    bool exit() const;

    // Cancels the last "exit" command, if any.
    void cancelExit();

    // Fires the built-in "exit" command, as if typed in the terminal.
    void setExit();

    //
    // Built-in commands:
    //

    struct BuiltInCmd
    {
        // Built-in commands are simple ones like "exit", "clear", etc, taking no arguments.
        void (*handler)(SimpleCommandTerminal & term);
        const char * name;
        const char * desc;
    };

    // User can query these, but not extend.
    int getBuiltInCommandCount() const;
    const BuiltInCmd * getBuiltInCommand(int index) const;
    const BuiltInCmd * getBuiltInCommand(const char * name) const;

protected:

    //
    // Event callbacks the implementations can extend:
    //

    // A no-op by default. Gets called when the built-in "exit" command
    // is run. A terminal impl may override this to perform custom cleanup
    // or even to cancelExit() and nullify the command.
    virtual void onExit();

    // Clipboard handling for CTRL+c/CTRL+v copy-pasting into the terminal's input line.
    // If get clipboard returns null or empty string, the terminal just ignores it.
    virtual void onSetClipboardString(const char * str);
    virtual const char * onGetClipboardString();

    //
    // Misc internal helpers:
    //

    // Set the input line as if the user typed something.
    void setLineBuffer(const char * str);
    const char * getLineBuffer() const;

    bool hasCmdNameInLineBuffer() const;
    bool isLineBufferEmpty() const;
    int  countNumArgsSoFar() const;

    void newLineNoMarker();   // New line WITHOUT the marker.
    void newLineWithMarker(); // New line WITH the marker.

    void clearVisibleEditLine(); // The line already in the terminal.
    void clearLineInputBuffer(); // The local input buffer.

    // Running a command after the user types [RETURN].
    bool finishCommand();
    void execCmdLine(const char * cmd);
    void addCmdToHistory(const char * cmd);

    // Sets the line input buffer from command history. No-op if no history.
    bool nextCmdFromHistory();
    bool prevCmdFromHistory();

    // Navigate the input text by one char. Used to handled left/right arrow keys.
    bool navigateTextLeft();
    bool navigateTextRight();
    void redrawInputLine();

    // Extended input handling for [CTRL]+char.
    bool handleCtrlKey(char chr);

    // Command/CVar completion triggered by a [TAB] key.
    bool tabCompletion();

    // Clears the input buffer and edit line after an [ESCAPE] key.
    bool discardInput();

    // Removes the char at the current cursor position in case of a [BACKSPACE].
    bool removeChar();

    // Add new printable character to the line input. Inserts at cursor pos.
    bool insertChar(char chr);

private:

    static const BuiltInCmd builtInCmds[];  // The built-in commands like "exit", "clear", etc. Fixed size array.
    static const int MaxCVarMatches = 64;   // Max CVars to print when doing a CVar name completion.
    static const int MaxArgMatches  = 64;   // Maximum number of argument matches to display on screen during arg completion.
    static const int MaxCmdMatches  = 32;   // Maximum number of command matches to display on screen.
    static const int MaxCmdsPerLine = 4;    // Max commands to list per line when doing cmd completion. Arg completion always lists 1 per line.
    static const int CmdHistoryMax  = 40;   // Max size in commands of the command history.
    static const int LineBufMaxSize = 2048; // Max length in chars of the line input buffer, including a null terminator.

    int lineBufUsed;                        // Chars of the line input buffer currently used.
    int lineBufInsertionPos;                // Insertion position of the next handled char.
    int currCmdInHistory;                   // Current command to be "viewed" in the history.
    int indexCmdHistory;                    // Index into 'cmdHistory' buffer for the next insertion. Also its size.
    bool lineHasMarker;                     // If a new input line has a marker yet or not.
    bool exitFlag;                          // Set by the "exit" built-in. Starts with 'false'.

    const SmallStr newLineMarkerStr;        // This string is printed at the start of every new input line.
    SmallStr cmdHistory[CmdHistoryMax];     // Buffer with recent lines typed into the console.
    char lineBuffer[LineBufMaxSize];        // Buffer to hold a line of input.
};

// ================================================================================================================

#include <cstdarg> // va_list/va_start/va_end
#include <cstdio>  // std::vsnprintf/std::fopen

// If this uses a path other than the CWD, the path must already exit.
#ifndef CFG_CMDHIST_FILE
#define CFG_CMDHIST_FILE "cmdhist.txt"
#endif // CFG_CMDHIST_FILE

// ========================================================
// BuiltInCmd handlers:
// ========================================================

namespace
{
void cmdExit(SimpleCommandTerminal & term)      { term.setExit();             }
void cmdClear(SimpleCommandTerminal & term)     { term.clear();               }
void cmdHistView(SimpleCommandTerminal & term)  { term.printCommandHistory(); }
void cmdHistClear(SimpleCommandTerminal & term) { term.clearCommandHistory(); }
void cmdHistSave(SimpleCommandTerminal & term)  { term.saveCommandHistory();  }
void cmdHistLoad(SimpleCommandTerminal & term)  { term.loadCommandHistory();  }
} // namespace {}

// ========================================================
// List of built-in commands:
// ========================================================

const SimpleCommandTerminal::BuiltInCmd SimpleCommandTerminal::builtInCmds[] =
{
    { &cmdExit,      "exit",      "Exits the interactive terminal mode." },
    { &cmdClear,     "clear",     "Clears the terminal screen."          },
    { &cmdHistView,  "histView",  "Prints the current command history."  },
    { &cmdHistClear, "histClear", "Erases the current command history."  },
    { &cmdHistSave,  "histSave",  "Saves the current command history to \"" CFG_CMDHIST_FILE "\"." },
    { &cmdHistLoad,  "histLoad",  "Load previous command history from \"" CFG_CMDHIST_FILE "\"."   }
};

// ========================================================
// class SimpleCommandTerminal:
// ========================================================

SimpleCommandTerminal::SimpleCommandTerminal(const char * newLineMark)
    : lineBufUsed(0)
    , lineBufInsertionPos(0)
    , currCmdInHistory(0)
    , indexCmdHistory(0)
    , lineHasMarker(false)
    , exitFlag(false)
    , newLineMarkerStr(newLineMark)
{
    CFG_CLEAR_ARRAY(lineBuffer);
}

SimpleCommandTerminal::~SimpleCommandTerminal()
{
    // Here to anchor the vtable to this file.
}

void SimpleCommandTerminal::printF(const char * fmt, ...)
{
    CFG_ASSERT(fmt != CFG_NULL);

    va_list vaList;
    char tempStr[LineBufMaxSize];

    va_start(vaList, fmt);
    const int result = std::vsnprintf(tempStr, CFG_ARRAY_LEN(tempStr), fmt, vaList);
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

    print(newLineMarkerStr.getCString());

    lineBufUsed         = 0;
    lineBufInsertionPos = 0;
    lineHasMarker       = true;

    CFG_CLEAR_ARRAY(lineBuffer);
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
    return CFG_NULL; // A no-op by default.
}

bool SimpleCommandTerminal::handleKeyInput(const int key, const char chr)
{
    switch (key)
    {
    case SpecialKey::Return     : return finishCommand();
    case SpecialKey::Tab        : return tabCompletion();
    case SpecialKey::Backspace  : return removeChar();
    case SpecialKey::UpArrow    : return nextCmdFromHistory();
    case SpecialKey::DownArrow  : return prevCmdFromHistory();
    case SpecialKey::RightArrow : return navigateTextRight();
    case SpecialKey::LeftArrow  : return navigateTextLeft();
    case SpecialKey::Escape     : return discardInput();
    case SpecialKey::Control    : return handleCtrlKey(chr);
    default                     : return insertChar(chr);
    } // switch (key)
}

int SimpleCommandTerminal::getCommandHistorySize() const
{
    return indexCmdHistory;
}

const char * SimpleCommandTerminal::getCommandFromHistory(const int index) const
{
    CFG_ASSERT(index >= 0 && index < getCommandHistorySize());
    return cmdHistory[index].getCString();
}

void SimpleCommandTerminal::clearCommandHistory()
{
    for (int i = 0; i < CmdHistoryMax; ++i)
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
    FILE * fileOut = CFG_NULL;

    #ifdef _MSC_VER
    fopen_s(&fileOut, CFG_CMDHIST_FILE, "wt");
    #else // _MSC_VER
    fileOut = std::fopen(CFG_CMDHIST_FILE, "wt");
    #endif // _MSC_VER

    if (fileOut == CFG_NULL)
    {
        printF("Unable to open \"%s\" for writing!\n", CFG_CMDHIST_FILE);
        return false;
    }

    const int histSize = getCommandHistorySize();
    for (int i = 0; i < histSize; ++i)
    {
        std::fprintf(fileOut, "%s\n", getCommandFromHistory(i));
    }

    printF("Command history saved to \"%s\".\n", CFG_CMDHIST_FILE);
    std::fclose(fileOut);
    return true;
}

bool SimpleCommandTerminal::loadCommandHistory()
{
    FILE * fileIn = CFG_NULL;

    #ifdef _MSC_VER
    fopen_s(&fileIn, CFG_CMDHIST_FILE, "rt");
    #else // _MSC_VER
    fileIn = std::fopen(CFG_CMDHIST_FILE, "rt");
    #endif // _MSC_VER

    if (fileIn == CFG_NULL)
    {
        printF("Unable to open command history file \"%s\".\n", CFG_CMDHIST_FILE);
        return false;
    }

    clearCommandHistory(); // Current history is lost, if any.

    char line[2048];
    while (std::fgets(line, CFG_ARRAY_LEN(line), fileIn))
    {
        addCmdToHistory(rightTrimString(line)); // RTrim to get rid of the newline.
    }

    printF("Command history restored from \"%s\".\n", CFG_CMDHIST_FILE);
    std::fclose(fileIn);
    return true;
}

int SimpleCommandTerminal::getBuiltInCommandCount() const
{
    return CFG_ARRAY_LEN(builtInCmds);
}

const SimpleCommandTerminal::BuiltInCmd * SimpleCommandTerminal::getBuiltInCommand(const int index) const
{
    CFG_ASSERT(index >= 0 && index < getBuiltInCommandCount());
    return &builtInCmds[index];
}

const SimpleCommandTerminal::BuiltInCmd * SimpleCommandTerminal::getBuiltInCommand(const char * name) const
{
    if (name == CFG_NULL || *name == '\0')
    {
        return CFG_NULL;
    }

    const int builtInCount = getBuiltInCommandCount();
    for (int i = 0; i < builtInCount; ++i)
    {
        #if CFG_CMD_CASE_SENSITIVE_NAMES
        if (std::strcmp(name, builtInCmds[i].name) == 0)
        #else // !CFG_CMD_CASE_SENSITIVE_NAMES
        if (compareStringsNoCase(name, builtInCmds[i].name) == 0)
        #endif // CFG_CMD_CASE_SENSITIVE_NAMES
        {
            return &builtInCmds[i];
        }
    }

    return CFG_NULL;
}

void SimpleCommandTerminal::setLineBuffer(const char * str)
{
    if (str == CFG_NULL)
    {
        return;
    }

    clearVisibleEditLine();
    if (*str != '\0')
    {
        print(str);
        lineBufUsed = copyString(lineBuffer, CFG_ARRAY_LEN(lineBuffer), str);
        lineBufInsertionPos = lineBufUsed;
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
    return lineBufUsed == 0;
}

bool SimpleCommandTerminal::hasCmdNameInLineBuffer() const
{
    //TODO
    return false;
}

int SimpleCommandTerminal::countNumArgsSoFar() const
{
    //TODO
    return 0;
}

void SimpleCommandTerminal::newLineNoMarker()
{
    print("\n");
    lineHasMarker = false;
}

void SimpleCommandTerminal::newLineWithMarker()
{
    printF("\n%s", newLineMarkerStr.getCString());
    lineHasMarker = true;
}

void SimpleCommandTerminal::clearVisibleEditLine()
{
    // Fill the line with blanks to clear it out.
    char blankLine[LineBufMaxSize];
    const int charsToFill = std::min(lineBufUsed + newLineMarkerStr.getLength(), CFG_ARRAY_LEN(blankLine));

    std::memset(blankLine, ' ', charsToFill);
    blankLine[charsToFill] = '\0';

    printF("\r%s\r%s", blankLine, newLineMarkerStr.getCString());
    lineHasMarker = true;
}

void SimpleCommandTerminal::clearLineInputBuffer()
{
    lineBufUsed = 0;
    lineBufInsertionPos = 0;
    lineBuffer[0] = '\0';
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

    if (!lineHasMarker && !exitFlag)
    {
        print(newLineMarkerStr.getCString());
        lineHasMarker = true;
    }

    return true; // Input (RETURN) was handled.
}

void SimpleCommandTerminal::execCmdLine(const char * cmd)
{
    // Watch for the built "exit" command:
    char cmdName[LineBufMaxSize];
    const char * cmdPtr = cmd;

    // Skip leading whitespace:
    for (; *cmdPtr != '\0' && isWhitespace(*cmdPtr); ++cmdPtr) { }

    // Copy until the first whitespace to get the command name:
    char * p = cmdName;
    for (; *cmdPtr != '\0' && !isWhitespace(*cmdPtr); ++p, ++cmdPtr)
    {
        *p = *cmdPtr;
    }
    *p = '\0';

    // Check the built-in commands first:
    const BuiltInCmd * builtIn = getBuiltInCommand(cmdName);
    if (builtIn != CFG_NULL)
    {
        builtIn->handler(*this);
        return;
    }

    // TODO handle a command:
    printF("exec '%s'\n", cmdName);
}

void SimpleCommandTerminal::addCmdToHistory(const char * cmd)
{
    if (indexCmdHistory < CmdHistoryMax) // Insert at front:
    {
        cmdHistory[indexCmdHistory++] = cmd;
    }
    else // Full, remove oldest:
    {
        CFG_ASSERT(indexCmdHistory == CmdHistoryMax);

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
    const char * cmd = cmdHistory[currCmdInHistory].getCString();
    if (currCmdInHistory > 0)
    {
        currCmdInHistory--;
    }

    setLineBuffer(cmd);
    return true;
}

bool SimpleCommandTerminal::prevCmdFromHistory()
{
    const char * cmd;
    if (currCmdInHistory != (indexCmdHistory - 1))
    {
        cmd = cmdHistory[++currCmdInHistory].getCString();
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
    if (lineBufInsertionPos <= 0)
    {
        return true; // Return whether the input was handled or not.
                     // In this case, always true.
    }

    lineBufInsertionPos--;
    redrawInputLine();
    return true;
}

bool SimpleCommandTerminal::navigateTextRight()
{
    if (lineBufInsertionPos >= lineBufUsed)
    {
        return true; // Same as above.
    }

    lineBufInsertionPos++;
    redrawInputLine();
    return true;
}

void SimpleCommandTerminal::redrawInputLine()
{
    // Workaround to position the cursor without
    // a gotoxy()-like function: redraw the whole line.

    char tempStr[LineBufMaxSize];
    std::memcpy(tempStr, lineBuffer, lineBufInsertionPos);
    tempStr[lineBufInsertionPos] = '\0';

    printF("\r%s%s", newLineMarkerStr.getCString(), tempStr);
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
             str != CFG_NULL && *str != '\0'; ++str)
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

bool SimpleCommandTerminal::tabCompletion()
{
    //TODO
    //NOTE: remember to take into account the built-ins!!!
    std::cout << __func__ << std::endl;

    return true;
}

bool SimpleCommandTerminal::discardInput()
{
    currCmdInHistory = indexCmdHistory - 1; // Reset command history traversal marker.
    setLineBuffer("");
    return true;
}

bool SimpleCommandTerminal::removeChar()
{
    if (isLineBufferEmpty() || lineBufInsertionPos <= 0)
    {
        return true; // Input is always handled by this call, so return true.
    }

    clearVisibleEditLine();

    if (lineBufInsertionPos == lineBufUsed) // Erasing last character only:
    {
        lineBufUsed--;
        lineBufInsertionPos--;

        lineBuffer[lineBufUsed] = '\0';
        print(lineBuffer);
    }
    else // Erasing at arbitrary position:
    {
        lineBufUsed--;
        lineBufInsertionPos--;

        int i;
        for (i = lineBufInsertionPos; i != lineBufUsed; ++i)
        {
            lineBuffer[i] = lineBuffer[i + 1];
        }
        lineBuffer[i] = '\0';

        // Position the cursor:
        print(lineBuffer);
        redrawInputLine();
    }

    return true;
}

bool SimpleCommandTerminal::insertChar(const char chr)
{
    if (!std::isprint(chr) || lineBufUsed >= (LineBufMaxSize - 1))
    {
        return false; // Not printable, don't consume the input event.
    }

    if (lineBufInsertionPos == lineBufUsed)
    {
        // Inserting at the end, usual case.
        lineBuffer[lineBufUsed++] = chr;
        lineBuffer[lineBufUsed] = '\0';
        lineBufInsertionPos++;

        // Print the char just pushed:
        printF("%c", chr);
    }
    else // Inserting at any other position:
    {
        int i;
        for (i = lineBufUsed; i > lineBufInsertionPos; --i)
        {
            lineBuffer[i] = lineBuffer[i - 1];
        }

        lineBuffer[i] = chr;
        lineBuffer[++lineBufUsed] = '\0';
        lineBufInsertionPos++;

        // Position the cursor:
        clearVisibleEditLine();
        print(lineBuffer);
        redrawInputLine();
    }

    return true;
}

//---------------------------------------------------------

#include <cassert>
#define TEST_ASSERT(expr) assert("Test assumption failed: " && (expr))

namespace unittest
{

// ========================================================
// SmallStr tests:
// ========================================================

void Test_SmallStr()
{
    SmallStr str;

    // Validate initial assumptions:
    TEST_ASSERT(str.getCString()[0] == '\0');
    TEST_ASSERT(str.isEmpty()       == true);
    TEST_ASSERT(str.isDynamic()     == false);
    TEST_ASSERT(str.getLength()     == 0);
    TEST_ASSERT(str.getCapacity()   >  0);

    str = "hello!";
    TEST_ASSERT(str.isEmpty()   == false);
    TEST_ASSERT(str.getLength() == 6);
    TEST_ASSERT(str == "hello!");

    str.clear();
    TEST_ASSERT(str.isEmpty()   == true);
    TEST_ASSERT(str.getLength() == 0);
    TEST_ASSERT(str == "");

    const char longStr[] = "------------------------------------ "
                           "A very long string to force memAlloc "
                           "------------------------------------\n";

    str = longStr;
    TEST_ASSERT(str.getCString()[0] == '-');
    TEST_ASSERT(str.isEmpty()       == false);
    TEST_ASSERT(str.isDynamic()     == true);
    TEST_ASSERT(str.getLength()     == CFG_ARRAY_LEN(longStr) - 1);
    TEST_ASSERT(str.getCapacity()   >= CFG_ARRAY_LEN(longStr));
    TEST_ASSERT(str == longStr);

    SmallStr other = str;
    TEST_ASSERT(other.getLength()   == str.getLength());
    TEST_ASSERT(other.getCapacity() == str.getCapacity());
    TEST_ASSERT(other.isDynamic()   == true); // Source str was dynamic
    TEST_ASSERT(other == str && other == longStr);

    //
    // Swap the dynamic strings:
    //
    str = "world!";
    TEST_ASSERT(str.isEmpty()   == false);
    TEST_ASSERT(str.isDynamic() == true);
    TEST_ASSERT(str.getLength() == 6);
    TEST_ASSERT(str == "world!");

    other = "hello";
    TEST_ASSERT(other.isEmpty()   == false);
    TEST_ASSERT(other.isDynamic() == true);
    TEST_ASSERT(other.getLength() == 5);
    TEST_ASSERT(other == "hello");

    swap(str, other);

    TEST_ASSERT(other == "world!");
    TEST_ASSERT(str   == "hello");
    TEST_ASSERT(str.getLength()   == 5);
    TEST_ASSERT(other.getLength() == 6);

    //
    // Swap again with small inline ones:
    //
    SmallStr s1("foo");
    SmallStr s2("bar");

    swap(s1, s2);

    TEST_ASSERT(s1 == "bar");
    TEST_ASSERT(s2 == "foo");

    //
    // Now swap dynamic and inline strings:
    //

    TEST_ASSERT(str.isDynamic() == true);
    TEST_ASSERT(s1.isDynamic()  == false);

    swap(str, s1);

    TEST_ASSERT(str == "bar");
    TEST_ASSERT(s1  == "hello");

    TEST_ASSERT(str.getLength() == 3);
    TEST_ASSERT(s1.getLength()  == 5);
}

// ========================================================
// LinkedHashTable tests:
// ========================================================

struct MyHtItem
    : public HashTableLink<MyHtItem>
{
    MyHtItem(const char * s) : name(s) { }
    std::string name;
};

template<class T, class HF>
void printHashTable(const LinkedHashTable<T, HF> & tab)
{
    const T * node = tab.getFirst();
    while (node != CFG_NULL)
    {
        std::cout << node->name.c_str() << " -> ";
        node = node->getNext();
    }
    std::cout << "~\n";
}

void Test_LinkedHashTable()
{
    LinkedHashTable<MyHtItem, StringHasher> tab;

    // Validate initial assumptions:
    TEST_ASSERT(tab.getSize()  == 0);
    TEST_ASSERT(tab.isEmpty()  == true);
    TEST_ASSERT(tab.getFirst() == CFG_NULL);

    MyHtItem a("a");
    MyHtItem b("b");
    MyHtItem c("c");
    MyHtItem d("d");
    MyHtItem e("e");
    MyHtItem f("f");

    //
    // Add items:
    //
    tab.linkWithKey(&a, a.name.c_str());
    tab.linkWithKey(&b, b.name.c_str());
    tab.linkWithKey(&c, c.name.c_str());
    tab.linkWithKey(&d, d.name.c_str());
    tab.linkWithKey(&e, e.name.c_str());
    tab.linkWithKey(&f, f.name.c_str());

    TEST_ASSERT(tab.getSize()  == 6);
    TEST_ASSERT(tab.isEmpty()  == false);
    TEST_ASSERT(tab.getFirst() == &f); // Last item inserted should be the head of the list

    //
    // Remove items:
    //
    tab.unlinkByKey("a");
    tab.unlinkByKey("f");

    TEST_ASSERT(tab.getSize()  == 4);
    TEST_ASSERT(tab.isEmpty()  == false);
    TEST_ASSERT(tab.getFirst() == &e);

    //
    // Find:
    //
    TEST_ASSERT(tab.findByKey("c") == &c);
    TEST_ASSERT(tab.findByKey("d") == &d);
    TEST_ASSERT(tab.findByKey("e") == &e);

    //
    // Unlink the rest, including inexistent keys:
    //
    tab.unlinkByKey("d");
    tab.unlinkByKey("b");
    tab.unlinkByKey("a");
    tab.unlinkByKey("b");
    tab.unlinkByKey("e");
    tab.unlinkByKey("c");

    TEST_ASSERT(tab.getSize()  == 0);
    TEST_ASSERT(tab.isEmpty()  == true);
    TEST_ASSERT(tab.getFirst() == CFG_NULL);
}

// ========================================================
// CVar System tests:
// ========================================================

void Test_CVarNameValidation()
{
    // Good names:
    TEST_ASSERT(CVarManager::isValidCVarName("hello")       == true);
    TEST_ASSERT(CVarManager::isValidCVarName("_hello")      == true);
    TEST_ASSERT(CVarManager::isValidCVarName("Hello123")    == true);
    TEST_ASSERT(CVarManager::isValidCVarName("_123_hello")  == true);
    TEST_ASSERT(CVarManager::isValidCVarName("hello_123")   == true);
    TEST_ASSERT(CVarManager::isValidCVarName("hello._123")  == true);
    TEST_ASSERT(CVarManager::isValidCVarName("Hello.World") == true);

    // Bad names:
    TEST_ASSERT(CVarManager::isValidCVarName("Hello World") == false);
    TEST_ASSERT(CVarManager::isValidCVarName("123Hello")    == false);
    TEST_ASSERT(CVarManager::isValidCVarName("123_hello")   == false);
    TEST_ASSERT(CVarManager::isValidCVarName("123.hello")   == false);
    TEST_ASSERT(CVarManager::isValidCVarName(".hello")      == false);
    TEST_ASSERT(CVarManager::isValidCVarName("hello.123")   == false);
    TEST_ASSERT(CVarManager::isValidCVarName("hello.#")     == false);
    TEST_ASSERT(CVarManager::isValidCVarName("hello.$")     == false);
    TEST_ASSERT(CVarManager::isValidCVarName("hello-@")     == false);
    TEST_ASSERT(CVarManager::isValidCVarName("!hello!")     == false);
}

void Test_CVarRegistration()
{
    CVarManager cvarMgr;

    // Make a couple vars dynamically allocated,
    // so the manager has a chance to free them.
    CVarStr * cvar0 = new CVarStr("cvar0", "0");
    CVarStr * cvar1 = new CVarStr("cvar1", "1");
    CVarStr * cvar2 = new CVarStr("cvar2", "2");
    CVarStr * cvar3 = new CVarStr("cvar3", "3");

    TEST_ASSERT(cvarMgr.registerCVar(cvar0)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(cvar1)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(cvar2)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(cvar3)  == true);
    TEST_ASSERT(cvarMgr.getRegisteredCount() == 4);

    // Can't register duplicates:
    TEST_ASSERT(cvarMgr.registerCVar(cvar0)  == false);
    TEST_ASSERT(cvarMgr.registerCVar(cvar1)  == false);
    TEST_ASSERT(cvarMgr.registerCVar(cvar2)  == false);
    TEST_ASSERT(cvarMgr.registerCVar(cvar3)  == false);
    TEST_ASSERT(cvarMgr.getRegisteredCount() == 4); // Shouldn't be changed on a failure.

    // Add a few static ones:
    CVarInt    cvar4("cvar4", 4,    "", CVar::External);
    CVarUInt   cvar5("cvar5", 5u,   "", CVar::External);
    CVarFloat  cvar6("cvar6", 6.6f, "", CVar::External);
    CVarDouble cvar7("cvar7", 7.7,  "", CVar::External);
    CVarBool   cvar8("cvar8", true, "", CVar::External);

    TEST_ASSERT(cvarMgr.registerCVar(&cvar4)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(&cvar5)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(&cvar6)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(&cvar7)  == true);
    TEST_ASSERT(cvarMgr.registerCVar(&cvar8)  == true);
    TEST_ASSERT(cvarMgr.getRegisteredCount()  == 9);

    // Remove a few:
    TEST_ASSERT(cvarMgr.removeCVar("foobar") == false); // Doesn't exit
    TEST_ASSERT(cvarMgr.removeCVar("cvar4")  == true);
    TEST_ASSERT(cvarMgr.removeCVar("cvar6")  == true);
    TEST_ASSERT(cvarMgr.removeCVar("cvar2")  == true);
    TEST_ASSERT(cvarMgr.removeCVar("cvar1")  == true);
    TEST_ASSERT(cvarMgr.getRegisteredCount() == 5);

    // Find the CVar::External ones:
    CVar * matches[10];
    const int found = cvarMgr.findCVarsWithFlags(CVar::External, matches, CFG_ARRAY_LEN(matches));
    TEST_ASSERT(found == 3); // cvar5, cvar7, cvar8

    // Find by exact name:
    TEST_ASSERT(cvarMgr.findCVar("cvar5") != CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar7") != CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar8") != CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar0") != CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar3") != CFG_NULL);

    // The remove ones can't be reached anymore.
    TEST_ASSERT(cvarMgr.findCVar("cvar4") == CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar6") == CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar2") == CFG_NULL);
    TEST_ASSERT(cvarMgr.findCVar("cvar1") == CFG_NULL);
}

void Test_CVars()
{
    Test_CVarNameValidation();
    Test_CVarRegistration();
    //TODO more tests!
}

// ========================================================
// Command System tests:
// ========================================================

void Test_Commands()
{
    //TODO
}

// ========================================================
// Test suite entry point:
// ========================================================

#define TEST(func)                               \
    do                                           \
    {                                            \
        std::cout << "- Test " << #func << "\n"; \
        Test_##func();                           \
        std::cout << "- Ok\n";                   \
    }                                            \
    CFG_ENDMACRO

void runAll()
{
    std::cout << "---- Running Unit Tests for the CFG Library ----\n";

    // Don't want the error output when testing. We have TEST_ASSERT.
    cfg::silentErrors = true;

    TEST(SmallStr);
    TEST(LinkedHashTable);
    TEST(Commands);
    TEST(CVars);

    // Restore to the initial default.
    cfg::silentErrors = false;

    std::cout << "All passed.\n";
}

#undef TEST

} // namespace unittest {}

//---------------------------------------------------------

void print_argv(int argc, const char ** argv)
{
    if (argc <= 1) return;
    printf("args:\n");
    for (int i = 0; i < argc; ++i)
    {
        printf("%s\n", argv[i]);
    }
    printf("\n");
}

void print_sizes()
{
    struct EmptyT {};
    std::cout << "sizeof(SmallStr)        = " << sizeof(SmallStr) << "\n";
    std::cout << "sizeof(CVarInterface)   = " << sizeof(CVar) << "\n";
    std::cout << "sizeof(CVarStr)         = " << sizeof(CVarStr) << "\n";
    std::cout << "sizeof(CVarBool)        = " << sizeof(CVarBool) << "\n";
    std::cout << "sizeof(CVarInt)         = " << sizeof(CVarInt) << "\n";
    std::cout << "sizeof(CVarFloat)       = " << sizeof(CVarFloat) << "\n";
    std::cout << "sizeof(CVarDouble)      = " << sizeof(CVarDouble) << "\n";
    std::cout << "sizeof(CVarManager)     = " << sizeof(CVarManager) << "\n";
    std::cout << "sizeof(HashTableLink)   = " << sizeof(HashTableLink<EmptyT>) << "\n";
    std::cout << "sizeof(LinkedHashTable) = " << sizeof(LinkedHashTable<EmptyT, StringHasher>) << "\n";
}

//---------------------------------------------------------

#include <iostream>  // std::cout
#include <atomic>    // std::atomic
#include <thread>    // std::thread
#include <cstdlib>   // std::system
#include <ctime>     // std::time/ctime

#include <unistd.h>  // isatty
#include <termios.h> // tcsetattr/tcgetattr

// ========================================================
// Colored text printing on the terminal:
// ========================================================

//TEMP
#define CFG_COLOR_PRINT 1

namespace color
{

inline bool canColorPrint()
{
#ifdef CFG_COLOR_PRINT
    return isatty(STDOUT_FILENO);
#else // !CFG_COLOR_PRINT
    return false;
#endif // CFG_COLOR_PRINT
}

// ANSI color codes:
inline const char * restore() { return canColorPrint() ? "\033[0;1m"  : ""; }
inline const char * red()     { return canColorPrint() ? "\033[31;1m" : ""; }
inline const char * green()   { return canColorPrint() ? "\033[32;1m" : ""; }
inline const char * yellow()  { return canColorPrint() ? "\033[33;1m" : ""; }
inline const char * blue()    { return canColorPrint() ? "\033[34;1m" : ""; }
inline const char * magenta() { return canColorPrint() ? "\033[35;1m" : ""; }
inline const char * cyan()    { return canColorPrint() ? "\033[36;1m" : ""; }
inline const char * white()   { return canColorPrint() ? "\033[37;1m" : ""; }

} // namespace color {}

//---------------------------------------------------------

//
// The input thread owns stdin
// The main thread owns stdout
//
class UnixTerminal CFG_FINAL_CLASS
    : public SimpleCommandTerminal
{
public:

    UnixTerminal();
    ~UnixTerminal();

    bool tty() const;
    bool hasInput() const;
    int  getInput();
    void flush();
    void printWelcomeMessage();

    void print(const char * text)   CFG_OVERRIDE;
    void printLn(const char * text) CFG_OVERRIDE;
    void clear()  CFG_OVERRIDE;
    void onExit() CFG_OVERRIDE;

    // Since clipboard handling requires system-specific code this
    // class just implements a simple application-side replacement
    // by keeping a local string. So it will work inside the terminal
    // but cannot be shared with any other applications.
    void onSetClipboardString(const char * str) CFG_OVERRIDE;
    const char * onGetClipboardString() CFG_OVERRIDE;

    // A little workaround for the Control key, which is only emulated for CTRL+p|n|l|c|v.
    // The lower eight bits have the char, the 15th bit the Control key flag (ushort expanded to int).
    static bool isCtrlKey(const int key) { return key >> 15; }

private:

    // Relying on system() is a major security hole, but this is
    // just a demo for the SimpleCommandTerminal, so I'm cool with it.
    static void sysCls() { std::system("clear"); }
    static unsigned short sysWaitChar();
    static void inputThreadFunction(UnixTerminal * term);

    // Terminal IO with the TERMIOS library:
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
    static const int InputBufferSize = 2048;
    unsigned short inputBuffer[InputBufferSize];

    // Local clipboard string, to avoid OS-specific code.
    // Unfortunately cannot be shared with external applications.
    SmallStr clipboardString;
};

UnixTerminal::UnixTerminal()
    : isATerminal(false)
    , quitInputThread(true)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    {
        CFG_ERROR("STDIN/STDOUT is not a TTY! UnixTerminal refuses to run in interactive mode.");
        return;
    }

    //
    // Set termios attributes for standard input:
    //

    if (tcgetattr(STDIN_FILENO, &oldTermAttr) != 0)
    {
        CFG_ERROR("Failed to get current terminal settings!");
        return;
    }

    newTermAttr = oldTermAttr;      // Make new settings same as old settings
    newTermAttr.c_lflag &= ~ICANON; // Disable buffered IO
    newTermAttr.c_lflag &= ~ECHO;   // Disable input echo
    newTermAttr.c_lflag &= ~ISIG;   // Disable CTRL+c interrupt signals, so we can use it for copy/paste
    newTermAttr.c_cc[VMIN] = 1;     // Minimum input required = 1 char

    if (tcsetattr(STDIN_FILENO, TCSANOW, &newTermAttr) != 0)
    {
        CFG_ERROR("Failed to set new terminal settings!");
        return;
    }

    isATerminal = true;
    quitInputThread = false;
    inputBufferInsertionPos = 0;
    CFG_CLEAR_ARRAY(inputBuffer);

    std::cout.sync_with_stdio(false);
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
        tcsetattr(STDIN_FILENO, TCSANOW, &oldTermAttr);
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
    CFG_ASSERT(term != CFG_NULL);

    // Keep checking for input until the terminal is shutdown.
    while (!term->quitInputThread && term->isATerminal)
    {
        if (term->inputBufferInsertionPos < UnixTerminal::InputBufferSize)
        {
            term->inputBuffer[term->inputBufferInsertionPos++] = sysWaitChar();
        }
    }
}

bool UnixTerminal::tty() const
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

void UnixTerminal::flush()
{
    // It's necessary to flush after input to make sure
    // the new characters a printed to screen immediately.
    // It seems some std::cout implementations are buffered...
    std::cout.flush();
}

void UnixTerminal::printWelcomeMessage()
{
    if (!isATerminal)
    {
        return;
    }

    sysCls();

    const std::time_t currTime = std::time(CFG_NULL);
    SmallStr timeStr(std::ctime(&currTime));
    timeStr[timeStr.getLength() - 1] = '\0'; // Pop the default newline added by ctime.

    printF("+----------%s Unix Terminal %s----------+\n"
           "|   Section started: %s   |\n"
           "|     %s      |\n"
           "+-----------------------------------+\n",
           color::cyan(), color::restore(),
           ttyname(STDIN_FILENO), timeStr.getCString());

    newLineWithMarker();
}

void UnixTerminal::print(const char * text)
{
    // We can print even if redirected to a file.
    if (text != CFG_NULL && *text != '\0')
    {
        std::cout << text;
    }
}

void UnixTerminal::printLn(const char * text)
{
    // We can print even if redirected to a file.
    // printLn() can take an empty string to just output the newline.
    if (text != CFG_NULL)
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

void UnixTerminal::onSetClipboardString(const char * str)
{
    clipboardString = str;
}

const char * UnixTerminal::onGetClipboardString()
{
    return clipboardString.getCString();
}

unsigned short UnixTerminal::sysWaitChar()
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
    case '\n' : return SpecialKey::Return;
    case '\r' : return SpecialKey::Return;
    case 0x7F : return SpecialKey::Backspace;
    case 0x09 : return SpecialKey::Tab;

    // These are hacks to catch CTRL+c|v, CTRL+p, CTRL+n, CTRL+l, respectively.
    // 15th bit in the 16bits word is the CTRL key flag, lower 8 are the key char.
    case 0x03 : return (1 << 15) | 'c';
    case 0x16 : return (1 << 15) | 'v';
    case 0x10 : return (1 << 15) | 'p';
    case 0x0E : return (1 << 15) | 'n';
    case 0x0C : return (1 << 15) | 'l';

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
                case 0x41 : return SpecialKey::UpArrow;
                case 0x42 : return SpecialKey::DownArrow;
                case 0x43 : return SpecialKey::RightArrow;
                case 0x44 : return SpecialKey::LeftArrow;
                default   : break;
                } // switch (c)
            }
            return SpecialKey::Escape;
        }
        break;
    } // switch (c)

    return static_cast<unsigned short>(c); // Any other key
}

//---------------------------------------------------------

void run_console()
{
    UnixTerminal unixTerm;

    while (unixTerm.tty() && !unixTerm.exit())
    {
        if (!unixTerm.hasInput())
        {
            continue;
        }

        int  key = unixTerm.getInput();
        char chr = key & 0xFF;

        // CTRL is a little more tricky on our Unix Terminal
        // setup and needs additional checking here.
        if (unixTerm.isCtrlKey(key))
        {
            key = SpecialKey::Control;
        }

        if (unixTerm.handleKeyInput(key, chr))
        {
            unixTerm.flush();
        }
    }
}

//---------------------------------------------------------

int main(int argc, const char ** argv)
{
    print_argv(argc, argv);
    print_sizes();

    unittest::runAll();

    run_console();
}
