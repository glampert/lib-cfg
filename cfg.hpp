
// ================================================================================================
// -*- C++ -*-
// File: cfg.hpp
// Author: Guilherme R. Lampert
// Created on: 09/06/16
// Brief: Lib CFG - A small C++ library for configuration file handling, CVars and Commands.
// ================================================================================================

#ifndef CFG_HPP
#define CFG_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <functional>
#include <type_traits>

// ========================================================
// Configurable library macros:
// ========================================================

//
// Overridable assert() for Lib CFG.
// Default to the standard assert() function.
//
#ifndef CFG_ASSERT
    #include <cassert>
    #define CFG_ASSERT assert
#endif // CFG_ASSERT

//
// Having format string checking is nice, but this
// is not a mandatory feature to compile the library.
//
#if defined(__GNUC__) || defined(__clang__)
    #define CFG_PRINTF_FUNC(fmtIndex, varIndex) __attribute__((format(printf, fmtIndex, varIndex)))
#else // !GNU && !Clang
    #define CFG_PRINTF_FUNC(fmtIndex, varIndex) /* unimplemented */
#endif // GNU | Clang

//
// All public members of the CFG library are defined inside this namespace.
//
namespace cfg
{

class CVar;
class Command;
class CVarManager;
class CommandManager;
class SimpleCommandTerminal;

// ================================================================================================
//
//                              CVars - Configuration Variables
//
// ================================================================================================

// ========================================================
// CVar callbacks:
// ========================================================

//
// Optional value completion callback for CVars.
// Only the C-style callback variant is available for CVars.
// If none is provided on registration, the default
// allowed values are used for value auto-completion.
//
// First parameter is the partial argument or an empty string.
// Second is the output array of matched strings.
// Third is the size of the output array of strings.
//
// The expected returned value is the numbers of matches available
// (which can be greater than the output size), or -1 if there was an error.
//
using CVarValueCompletionCallback = int (*)(const char *, std::string *, int);

//
// Callback passed to CVarManager::enumerateAllCVars().
//
// First argument is the CVar being visited.
// Second argument is a user-provided "context" pointer
// that is forwarded from the enumerateAllCVars() call.
//
// Return true to continue the enumeration or false to break it.
//
using CVarEnumerateCallback = bool (*)(CVar *, void *);

// ========================================================
// class CVar:
// ========================================================

class CVar
{
public:

    //
    // Predefined CVar flags (user can extend these).
    //
    struct Flags final
    {
        // Marked as modified to eventually be written to a configuration file (internal use flag).
        static constexpr std::uint32_t Modified    = 1 << 0;

        // Value persists after program termination and is restored in a future restart (saved to config file).
        static constexpr std::uint32_t Persistent  = 1 << 1;

        // Value persists only while the program is running (never saved). Mutually exclusive with 'Persistent'.
        static constexpr std::uint32_t Volatile    = 1 << 2;

        // For display only. Cannot be changed via Console/command line or the set*() methods.
        static constexpr std::uint32_t ReadOnly    = 1 << 3;

        // Similar to 'ReadOnly' but can be set by the startup command line of the application.
        static constexpr std::uint32_t InitOnly    = 1 << 4;

        // Enforces min/max bounds for number vars and allowed strings for string vars.
        static constexpr std::uint32_t RangeCheck  = 1 << 5;

        // Variable was defined by the user via a 'set' command in the Console or via a config file.
        static constexpr std::uint32_t UserDefined = 1 << 6;
    };

    //
    // Type category flags.
    //
    enum class Type : std::uint8_t
    {
        Int,
        Bool,
        Float,
        String,
        Enum
    };

    //
    // Number formatting/representations.
    // Use these with 'setNumberFormat()' to define how number CVar are converted
    // to strings and how string CVars are rendered when initialized from numbers.
    //
    enum class NumberFormat : std::uint8_t
    {
        Binary,
        Octal,
        Decimal,
        Hexadecimal
    };

public:

    //
    // CVar interface:
    //

    virtual ~CVar();

    // Name and description cannot be changed after the CVar is created.
    // Description is optional. If no description was provided, an empty string is returned.
    virtual std::string getName() const = 0;
    virtual std::string getDesc() const = 0;
    virtual const char * getNameCString() const = 0;
    virtual const char * getDescCString() const = 0;

    // Get a printable string of the underlaying type for displaying.
    // E.g.: "int" for integers, "float" for floats, "string" for strings, etc.
    virtual std::string getTypeString()   const = 0;
    virtual const char * getTypeCString() const = 0;

    // Get the type category of the underlaying wrapped value.
    // Type cannot be changed once a CVar is constructed.
    virtual Type getType() const = 0;

    // Set the formatting used to convert numerical CVar values to
    // printable strings, i.e.: binary, octal, decimal, hexadecimal.
    // Initial formatting is decimal.
    virtual NumberFormat getNumberFormat() const = 0;
    virtual void setNumberFormat(NumberFormat format) = 0;

    // User is free to query and change the CVar flags after construction.
    virtual std::uint32_t getFlags() const = 0;
    virtual void setFlags(std::uint32_t newFlags) = 0;

    // Var flags to printable string or an empty string if flags = 0.
    virtual std::string getFlagsString() const = 0;

    // Direct access to the modified flag.
    virtual void setModified()   = 0;
    virtual void clearModified() = 0;

    // Test specific flags:
    virtual bool isModified()     const = 0;
    virtual bool isWritable()     const = 0;
    virtual bool isPersistent()   const = 0;
    virtual bool isRangeChecked() const = 0;

    // std::strcmp-style comparison of just the variable names.
    virtual int compareNames(const CVar & other) const = 0;

    // Does a deep comparison to determine if both instances are equivalent.
    virtual bool compareEqual(const CVar & other) const = 0;

    //
    // CVar value access:
    //

    // The following set*() methods will fail with an error message
    // if the variable is read-only or if there's no type conversion
    // available. The get*() methods will fail with an error message
    // if there is no conversion but sill return a default value
    // (zero for numbers / false for boolean / empty string).

    virtual std::int64_t getIntValue() const = 0;
    virtual bool setIntValue(std::int64_t newValue) = 0;

    virtual bool getBoolValue() const = 0;
    virtual bool setBoolValue(bool newValue) = 0;

    virtual double getFloatValue() const = 0;
    virtual bool setFloatValue(double newValue) = 0;

    virtual std::string getStringValue() const = 0;
    virtual bool setStringValue(std::string newValue) = 0;

    //
    // Allowed value ranges and default/reset value:
    //

    // Get the allowed variable values as strings for displaying in the console/terminal.
    // It will write up to 'maxValueStrings' in the output array. The returned value is the
    // numbers of value strings available (which can be > maxValueStrings), or -1 if there was an error.
    virtual int getAllowedValueStrings(std::string * outValueStrings, int maxValueStrings) const = 0;

    // Get the total number of allowed variable values or zero if none.
    // You can use this to allocate storage for getAllowedValueStrings().
    virtual int getAllowedValueCount() const = 0;

    // Rests the CVar to its default value and flags it as modified.
    // ReadOnly/InitOnly CVars cannot be reset, not even to defaults.
    virtual bool setDefaultValue() = 0;

    // Get the default reset value as a string for Console/Terminal printing.
    virtual std::string getDefaultValueString() const = 0;

    // Similar to getAllowedValueStrings(), but will forward to an argument completion callback
    // first if the CVar has one. If no callback is set, then it returns the allowed values.
    virtual int valueCompletion(const char * partialVal, std::string * outMatches, int maxMatches) const = 0;
    virtual CVarValueCompletionCallback getValueCompletionCallback() const = 0;
};

// ========================================================
// class CVarManager:
// ========================================================

class CVarManager
{
public:

    //
    // Allocator/factory:
    //

    static CVarManager * createInstance(int cvarHashTableSizeHint = 0);
    static void destroyInstance(CVarManager * cvarManager);

    //
    // CVarManager interface:
    //

    // Destructor will also destroy all CVars registered with this manager.
    virtual ~CVarManager();

    // Finds previously registered CVar or returns null if no such var is registered.
    virtual CVar * findCVar(const char * name) const = 0;

    // Find vars with name starting with the 'partialName' substring.
    // Returns the total number of matches found, which might be > than maxMatches,
    // but only up to maxMatches will be written to the output array in any case.
    // Returns a negative value in case of error. Output variables are sorted
    // alphabetically according to name.
    virtual int findCVarsWithPartialName(const char * partialName,
                                         CVar ** outMatches,
                                         int maxMatches) const = 0;

    // Same as above, but get only the var names for displaying.
    virtual int findCVarsWithPartialName(const char * partialName,
                                         const char ** outMatches,
                                         int maxMatches) const = 0;

    // Similar to findCVarsWithPartialName(), but instead searches by var flags.
    virtual int findCVarsWithFlags(std::uint32_t flags,
                                   CVar ** outMatches,
                                   int maxMatches) const = 0;

    // Remove previously registered var by name or pointer. If this function
    // succeeds, all pointers to the variable will also become invalid!
    virtual bool removeCVar(const char * name) = 0;
    virtual bool removeCVar(CVar * cvar) = 0;

    // Unregisters and deletes all CVar instances that belong to this manager.
    // Be careful when calling this method, since all external CVar pointers will
    // be invalidated. The effect is equivalent to destroying the manager instance.
    virtual void removeAllCVars() = 0;

    // Number of CVar registered with this manager so far.
    virtual int getRegisteredCVarsCount() const = 0;

    // Tests if a string complies with the CVar naming rules.
    // It DOES NOT check if the variable is already registered.
    virtual bool isValidCVarName(const char * name) const = 0;

    // Calls the supplied callback for each registered CVar.
    // No specific iteration order is guaranteed.
    virtual void enumerateAllCVars(CVarEnumerateCallback enumCallback, void * userContext) = 0;

    //
    // CVar registration:
    //
    // These will fail with an error and return null if a CVar
    // with the same name already exits. Strings are copied, so
    // you can throw away the pointers once the method returns.
    //

    virtual CVar * registerCVarBool(const char * name, const char * description, std::uint32_t flags,
                                    bool initValue, CVarValueCompletionCallback completionCb = nullptr) = 0;

    virtual CVar * registerCVarInt(const char * name, const char * description, std::uint32_t flags,
                                   std::int64_t initValue, std::int64_t minValue, std::int64_t maxValue,
                                   CVarValueCompletionCallback completionCb = nullptr) = 0;

    virtual CVar * registerCVarFloat(const char * name, const char * description, std::uint32_t flags,
                                     double initValue, double minValue, double maxValue,
                                     CVarValueCompletionCallback completionCb = nullptr) = 0;

    virtual CVar * registerCVarString(const char * name, const char * description, std::uint32_t flags,
                                      const std::string & initValue, const char ** allowedStrings,
                                      CVarValueCompletionCallback completionCb = nullptr) = 0;

    virtual CVar * registerCVarEnum(const char * name, const char * description, std::uint32_t flags,
                                    std::int64_t initValue, const std::int64_t * enumConstants, const char ** constNames,
                                    CVarValueCompletionCallback completionCb = nullptr) = 0;

    //
    // CVar value queries:
    //
    // Get the value of a CVar by its name.
    // If the CVar doesn't exit, and error is logged and
    // you get a default value (zero/empty string).
    //

    virtual bool getCVarValueBool(const char * name) const = 0;
    virtual std::int64_t getCVarValueInt(const char * name) const = 0;
    virtual double getCVarValueFloat(const char * name) const = 0;
    virtual std::string getCVarValueString(const char * name) const = 0;

    //
    // CVar value update with registration:
    //
    // Set the value of an existing CVar by its name or
    // register a new CVar with defaults. The returned
    // pointer will be the existing CVar that was set or
    // the newly created one.
    //

    virtual CVar * setCVarValueBool(const char * name, bool value, std::uint32_t flags) = 0;
    virtual CVar * setCVarValueInt(const char * name, std::int64_t value, std::uint32_t flags) = 0;
    virtual CVar * setCVarValueFloat(const char * name, double value, std::uint32_t flags) = 0;
    virtual CVar * setCVarValueString(const char * name, const std::string & value, std::uint32_t flags) = 0;
};

// ================================================================================================
//
//                                      Console Commands
//
// ================================================================================================

// ========================================================
// Command system constants:
// ========================================================

constexpr int  MaxCommandNameLength   = 32;     // Maximum length in chars of a command name, including the '\0'.
constexpr int  MaxCommandDescLength   = 100;    // Max length in chars of the description comment user can attach to a command.
constexpr int  MaxCommandArgStrLength = 2048;   // Maximum length in chars of a string of arguments, including the '\0'.
constexpr int  MaxCommandArguments    = 64;     // Maximum number of argument strings for a single command.
constexpr int  MaxReentrantCommands   = 999999; // If this many commands are executed in a single frame, there's probably a reentrant loop.
constexpr int  CommandBufferSize      = 65535;  // Max length in chars of the command buffer used by CommandManager, including a '\0'.
constexpr char CommandTextSeparator   = ';';    // Character assumed to be the separator between different commands on the same line.

// Command execution modes for CommandManager::execute() and friends.
// See also CommandManager::execNow(), execInsert() and execAppend().
enum class CommandExecMode
{
    Immediate, // Immediate execution. Doesn't return until completed.
    Insert,    // Insert at current position (front) of the command buffer, but doesn't run it yet.
    Append     // Append to end of the command buffer for future execution by execBufferedCommands().
};

// ========================================================
// class CommandArgs:
// ========================================================

class CommandArgs final
{
public:

    // Creates an empty CommandArgs.
    CommandArgs();

    // Construct from a string of arguments separated by whitespace.
    // The first argument extracted is assumed to be the command/program
    // name and will be available via 'getCommandName()' but won't be included
    // in the argStrings[]. The command string may be preceded and followed by any
    // number of whitespace chars. Sequences of quoted characters are assumed
    // to be strings and are not split by whitespace, producing one argument.
    // Single and double quotes can make a quoted block. Single quotes can
    // appear inside a double-quoted block.
    explicit CommandArgs(const char * cmdStr);

    // Construct from a pre-split argc/argv array of strings, Unix-style.
    // This can be used to copy the command line arguments from the main() function.
    // First entry is assumed to be the program name; 'argc' is expected to be >= 1.
    CommandArgs(int argc, const char * argv[]);

    // Copy/assignment.
    CommandArgs(const CommandArgs & other);
    CommandArgs & operator = (const CommandArgs & other);

    // Get the first argument (the command/program name).
    const char * getCommandName() const noexcept;

    // Number of argument strings parsed, NOT including the command/program name.
    int getArgCount() const noexcept;

    // Returns true if there are no arguments (argCount == 0).
    bool isEmpty() const noexcept;

    // Get the individual argument strings.
    // Fails with an assertion if the index is out-of-bounds.
    const char * operator[](int index) const;
    const char * getArgAt(int index)   const;

    // Begin/end range to allow "range-based for" iteration of arguments.
    // No validation done if you deref the end or a past-the-end pointer!
    const char * const * begin() const noexcept;
    const char * const * end()   const noexcept;

    // Compares a given argument index with the provided string.
    // Return is equivalent to std::strcmp(). If argIndex < 0 || >= argCount, -1 is returned.
    int compare(int argIndex, const char * str) const;

private:

    void parseArgString(const char * argStr);
    bool addArgString(const char * argStr);
    const char * appendToken(const char * token, int tokenLen);

    // Number of arguments parsed and inserted in the argStrings[] array.
    int argCount;

    // Index to the start of the next free slot in tokenizedArgStr[].
    // Only used during command line parsing.
    int nextTokenIndex;

    // Extra pointer to the command/program name, which is assumed to be
    // the first string of tokenizedArgStr[]. argStrings[] will refer to the
    // parameters that follow the command name but won't include this name.
    const char * cmdName;

    // Array of arguments. Each one points to a slice of tokenizedArgStr[].
    const char * argStrings[MaxCommandArguments];

    // Tokenized string of arguments. Each arg is separated by a null char.
    // Each entry in argStrings[] points to a slice of this buffer.
    char tokenizedArgStr[MaxCommandArgStrLength];
};

// ========================================================
// Command callbacks:
// ========================================================

//
// Callback passed to CommandManager::enumerateAllCommands().
//
// First argument is the Command being visited.
// Second argument is a user-provided "context" pointer
// that is forwarded from the enumerateAllCommands() call.
//
// Return true to continue the enumeration or false to break it.
//
using CommandEnumerateCallback = bool (*)(Command *, void *);

//
// The "callback" interface uses plain function pointers.
// The last void* parameter is an optional pointer to a
// user provided "context" object.
//
using CommandHandlerCallback = void (*)(const CommandArgs &, void *);
using CommandArgCompletionCallback = int (*)(const char *, std::string *, int, void *);

//
// "Delegates" are the generic std::function interface,
// which can bind to plain functions or stateful lambdas.
//
using CommandHandlerDelegate = std::function<void(const CommandArgs &)>;
using CommandArgCompletionDelegate = std::function<int(const char *, std::string *, int)>;

//
// This specialized template class is a delegate for
// command handlers from pointers to member functions.
//
class CommandHandlerMemFunc final
{
public:

    CommandHandlerMemFunc(const CommandHandlerMemFunc & other)
    {
        CFG_ASSERT(other.baseHolder != nullptr);
        baseHolder = other.baseHolder->clone(&dataBlob);
    }

    CommandHandlerMemFunc & operator = (const CommandHandlerMemFunc & other)
    {
        CFG_ASSERT(other.baseHolder != nullptr);
        baseHolder = other.baseHolder->clone(&dataBlob);
        return *this;
    }

    template<typename RetType, typename ClassType, typename... Args>
    CommandHandlerMemFunc(ClassType * objPtr, RetType (ClassType::*pMemFunc)(Args...))
    {
        const auto holder = MemFuncHolder<RetType, ClassType, Args...>{ objPtr, pMemFunc };
        baseHolder = holder.clone(&dataBlob);
    }

    template<typename RetType, typename ClassType, typename... Args>
    CommandHandlerMemFunc(const ClassType * const objPtr, RetType (ClassType::*pMemFunc)(Args...) const)
    {
        const auto holder = MemFuncHolder<RetType, typename std::add_const<ClassType>::type, Args...>{ objPtr, pMemFunc };
        baseHolder = holder.clone(&dataBlob);
    }

    template<typename RetType, typename... Args>
    RetType invoke(Args&&... args) const
    {
        const auto holder = static_cast<const CallableHolder<RetType, Args...> *>(baseHolder);
        return holder->invoke(std::forward<Args>(args)...);
    }

    // nullptr interop:
    CommandHandlerMemFunc(std::nullptr_t) noexcept { }
    bool operator == (std::nullptr_t) const noexcept { return baseHolder == nullptr; }
    bool operator != (std::nullptr_t) const noexcept { return baseHolder != nullptr; }

private:

    struct BaseCallableHolder
    {
        virtual BaseCallableHolder * clone(void * where) const = 0;
        virtual ~BaseCallableHolder();
    };

    template<typename RetType, typename... Args>
    struct CallableHolder : BaseCallableHolder
    {
        virtual RetType invoke(Args&&... args) const = 0;
    };

    template<typename RetType, typename ClassType, typename... Args>
    struct MemFuncHolder final : CallableHolder<RetType, Args...>
    {
        // This selector will use the const-qualified method if
        // ClassType is const, the non-const otherwise.
        using MemFunc     = RetType (ClassType::*)(Args...);
        using CMemFunc    = RetType (ClassType::*)(Args...) const;
        using MemFuncType = typename std::conditional<std::is_const<ClassType>::value, CMemFunc, MemFunc>::type;

        ClassType * objPtr;
        MemFuncType pMemFunc;

        MemFuncHolder(ClassType * obj, MemFuncType pmf)
            : objPtr(obj)
            , pMemFunc(pmf)
        {
            CFG_ASSERT(objPtr   != nullptr);
            CFG_ASSERT(pMemFunc != nullptr);
        }

        RetType invoke(Args&&... args) const override
        {
            return (objPtr->*pMemFunc)(std::forward<Args>(args)...);
        }

        BaseCallableHolder * clone(void * where) const override
        {
            // Placement constructed in the external memory buffer.
            return ::new(where) MemFuncHolder<RetType, ClassType, Args...>(*this);
        }
    };

private:

    BaseCallableHolder * baseHolder = nullptr;

    class Dummy;
    using GenericMemFuncHolder = MemFuncHolder<void *, Dummy, void *>;

    // MemFuncHolder instances are constructed in-place into this
    // buffer, so we don't have allocate any extra memory for them.
    alignas(GenericMemFuncHolder)
    std::uint8_t dataBlob[sizeof(GenericMemFuncHolder)]{};
};

//
// Use these to create command handlers from a pointer to an
// object and pointer to the member function that will handle
// the command. The member func can optionally be const qualified.
//
// The pointers must not be null.
//
// Example:
//  MyClass obj{};
//  auto pmf = makeMemFuncCommandHandler(&obj, &MyClass::someMethod);
//

template<typename ClassType>
CommandHandlerMemFunc makeMemFuncCommandHandler(ClassType * objPtr, void (ClassType::*pCmdExec)(const CommandArgs &))
{
    return CommandHandlerMemFunc(objPtr, pCmdExec);
}
template<typename ClassType>
CommandHandlerMemFunc makeMemFuncCommandHandler(const ClassType * const objPtr, void (ClassType::*pCmdExec)(const CommandArgs &) const)
{
    return CommandHandlerMemFunc(objPtr, pCmdExec);
}

template<typename ClassType>
CommandHandlerMemFunc makeMemFuncCommandArgCompletion(ClassType * objPtr, int (ClassType::*pArgCompl)(const char *, std::string *, int))
{
    return CommandHandlerMemFunc(objPtr, pArgCompl);
}
template<typename ClassType>
CommandHandlerMemFunc makeMemFuncCommandArgCompletion(const ClassType * const objPtr, int (ClassType::*pArgCompl)(const char *, std::string *, int) const)
{
    return CommandHandlerMemFunc(objPtr, pArgCompl);
}

// ========================================================
// class Command:
// ========================================================

class Command
{
public:

    virtual ~Command();

    virtual std::uint32_t getFlags() const = 0;
    virtual void setFlags(std::uint32_t newFlags) = 0;

    virtual const char * getNameCString() const = 0;
    virtual const char * getDescCString() const = 0;
    virtual std::string getName() const = 0;
    virtual std::string getDesc() const = 0;

    virtual int getMinArgs() const = 0;
    virtual int getMaxArgs() const = 0;
    virtual bool isAlias()   const = 0;

    // Calls the command argument completion callback, if any.
    // It will write up to 'maxMatches' in the output array. The returned value is the total
    // numbers of matches available (which can be > maxMatches), or -1 if there was an error.
    virtual int argumentCompletion(const char * partialArg, std::string * outMatches, int maxMatches) const = 0;
};

// ========================================================
// class CommandManager:
// ========================================================

class CommandManager
{
public:

    //
    // Allocator/factory:
    //

    static CommandManager * createInstance(int cmdHashTableSizeHint = 0, CVarManager * cvarMgr = nullptr);
    static void destroyInstance(CommandManager * cmdManager);

    //
    // Constants:
    //

    // Pass this constant to 'execBufferedCommands()' to indicate
    // all the available commands should be processed.
    static constexpr std::uint32_t ExecAll = ~0u;

    // Pass this constant to 'disableCommandsWithFlags()' to indicate
    // all commands, independent of user flags, should be disabled.
    // Any further command text input will then be ignored.
    static constexpr std::uint32_t DisableAll = ~0u;

    //
    // CommandManager interface:
    //

    // Destructor will also destroy all commands registered with this manager.
    virtual ~CommandManager();

    // Finds previously registered command or returns null if no such command is registered.
    virtual Command * findCommand(const char * name) const = 0;

    // Find commands with name starting with the 'partialName' substring.
    // Returns the total number of matches found, which might be > than maxMatches,
    // but only up to maxMatches will be written to the output array in any case.
    // Returns a negative value in case of error. Output commands are sorted
    // alphabetically according to name.
    virtual int findCommandsWithPartialName(const char * partialName,
                                            Command ** outMatches,
                                            int maxMatches) const = 0;

    // Same as above, but get only the var names for displaying.
    virtual int findCommandsWithPartialName(const char * partialName,
                                            const char ** outMatches,
                                            int maxMatches) const = 0;

    // Similar to findCommandsWithPartialName(), but instead searches by commands flags.
    virtual int findCommandsWithFlags(std::uint32_t flags,
                                      Command ** outMatches,
                                      int maxMatches) const = 0;

    // Remove previously registered command by name or pointer. If this function
    // succeeds, all pointers to the command instance will also become invalid!
    virtual bool removeCommand(const char * name) = 0;
    virtual bool removeCommand(Command * cmd) = 0;

    // Removes the command if it is an alias. Fails if the name refers to a normal command.
    virtual bool removeCommandAlias(const char * aliasName) = 0;

    // Unregisters and deletes all command instances that belong to this manager.
    // Be careful when using this method, since all external Command pointers will
    // be invalidated. The effect is equivalent to destroying the manager instance.
    virtual void removeAllCommands() = 0;

    // Unregisters all current command aliases. Normal commands are not touched.
    virtual void removeAllCommandAliases() = 0;

    // Number of commands registered with this manager so far.
    virtual int getRegisteredCommandsCount() const = 0;

    // Get just the number of command aliases.
    virtual int getCommandAliasCount() const = 0;

    // Tests if a string complies with the command naming rules.
    // It DOES NOT check if the command is already registered.
    virtual bool isValidCommandName(const char * name) const = 0;

    // Calls the supplied callback for each registered command.
    // No specific iteration order is guaranteed.
    virtual void enumerateAllCommands(CommandEnumerateCallback enumCallback, void * userContext) = 0;

    // All commands containing any of the specified flags will be
    // prevented from executing when processing command text.
    // 'DisableAll' prevents all commands from executing.
    virtual void disableCommandsWithFlags(std::uint32_t flags) = 0;

    // Restores the execution of all commands, regardless of flags.
    virtual void enableAllCommands() = 0;

    // Set or query the helper CVarManager associated with this CommandManager.
    virtual CVarManager * getCVarManager() const = 0;
    virtual void setCVarManager(CVarManager * cvarMgr) = 0;

    //
    // Command registration / command aliases:
    //

    // Register with C-style function callback handlers.
    virtual bool registerCommand(const char * name,
                                 CommandHandlerCallback handler,
                                 CommandArgCompletionCallback completionHandler = nullptr,
                                 void * userContext       = nullptr,
                                 const char * description = "",
                                 std::uint32_t flags      =  0,
                                 int minArgs              = -1,
                                 int maxArgs              = -1) = 0;

    // Register with delegate handlers (lambdas with possible capture).
    virtual bool registerCommand(const char * name,
                                 CommandHandlerDelegate handler,
                                 CommandArgCompletionDelegate completionHandler = nullptr,
                                 const char * description = "",
                                 std::uint32_t flags      =  0,
                                 int minArgs              = -1,
                                 int maxArgs              = -1) = 0;

    // Register with pointer-to-member-function handlers.
    virtual bool registerCommand(const char * name,
                                 CommandHandlerMemFunc handler,
                                 CommandHandlerMemFunc completionHandler = nullptr,
                                 const char * description = "",
                                 std::uint32_t flags      =  0,
                                 int minArgs              = -1,
                                 int maxArgs              = -1) = 0;

    // Create an alias for a command string. Execution mode for
    // each time the command alias is invoked can also be provided.
    virtual bool createCommandAlias(const char * aliasName,
                                    const char * aliasedCmdStr,
                                    CommandExecMode execMode,
                                    const char * description = "") = 0;

    //
    // Command text execution:
    //

    // Executes the given command string immediately.
    // Text will not be added to the command buffer.
    virtual void execNow(const char * str) = 0;

    // Inserts the command text at the front of the command buffer (prepends it).
    // Text will not be executed nor validated until a future 'execBufferedCommands()' call.
    virtual void execInsert(const char * str) = 0;

    // Appends the command text to the end of the command buffer.
    // Text will not be executed nor validated until a future 'execBufferedCommands()' call.
    virtual void execAppend(const char * str) = 0;

    // Execute a command string with any of the available modes.
    virtual void execute(CommandExecMode execMode, const char * str) = 0;

    // Check if we have pending command text to execute.
    virtual bool hasBufferedCommands() const = 0;

    // Execute the command buffer text. You can specify a maximum number of commands
    // to execute in the call or allow all buffered commands to execute with 'ExecAll'.
    // Returns the number of commands executed.
    virtual int execBufferedCommands(std::uint32_t maxCommandsToExec = ExecAll) = 0;

    // Tries to load and execute the given configuration file.
    // Same rules of command strings apply. Lines are assumed to be
    // whole commands, unless a CommandTextSeparator (;) is found.
    // Commands are executed immediately, so the file contents will
    // not go though the command buffer. Errors will not stop the execution
    // of the file, only the compromised commands are usually affected.
    // If a SimpleCommandTerminal pointer is provided, each executed
    // command will also be echoed to that terminal.
    virtual bool execConfigFile(const char * filename, SimpleCommandTerminal * term) = 0;

    // Process the program command line provided at initialization.
    // 'set' and 'reset' commands (modifying CVars) are executed immediately, while
    // other commands are buffered and executed when the command buffer is next flushed.
    // Setting CVars from the startup command line will allow modifying InitOnly CVars.
    //
    // '+' characters separate the command line string into multiple commands.
    // All of these are valid:
    // ./prog +set test blah +foo test
    // ./prog set test blah + foo test
    //
    // Will produce two command lines:
    //  "set test blah"
    // and
    //  "foo test"
    virtual void execStartupCommandLine(int argc, const char * argv[]) = 0;
};

//
// Default commands besides the SimpleCommandTerminal built-ins.
// Includes the common utilities for manipulating CVars and saving
// and loading configuration files. The terminal is the destination
// output of the commands and must be provided. A CommandManager is
// also mandatory. This function becomes a no-op if the defaults are
// disabled at compilation by the 'CFG_NO_DEFAULT_COMMANDS' macro.
//
void registerDefaultCommands(CommandManager * cmdManager, SimpleCommandTerminal * term);

// ================================================================================================
//
//                                 Interactive Terminal/Console
//
// ================================================================================================

// ========================================================
// Special keys for console keyboard input:
// ========================================================

struct SpecialKeys final
{
    enum
    {
        Return      = 1 << 8,
        Tab         = 1 << 9,
        Backspace   = 1 << 10,
        Delete      = 1 << 11,
        UpArrow     = 1 << 12,
        DownArrow   = 1 << 13,
        RightArrow  = 1 << 14,
        LeftArrow   = 1 << 15,
        Escape      = 1 << 16,
        Control     = 1 << 17
        // Other keys use the equivalent ASCII character,
        // so these constants skip the first 8bits.
    };
    static std::string toString(int key);
};

// ========================================================
// class SimpleCommandTerminal:
// ========================================================

class SimpleCommandTerminal
{
public:

    //
    // Command Terminal interface:
    //

    // Not copyable.
    SimpleCommandTerminal(const SimpleCommandTerminal &) = delete;
    SimpleCommandTerminal & operator = (const SimpleCommandTerminal &) = delete;

    virtual ~SimpleCommandTerminal();

    // 'newlineMark' is the character(s) printed at the start of every new input line.
    // The managers are optional and may be null. If a manager is null, that functionality is disabled.
    SimpleCommandTerminal(CommandManager * cmdMgr      = nullptr,
                          CVarManager    * cvarMgr     = nullptr,
                          const char     * newlineMark = "> ");

    // Prints an ASCII string to the console.
    virtual void print(const char * text) = 0;

    // Similar to print() but appends a newline by default.
    virtual void printLn(const char * text) = 0;

    // Printf-style text printing. No default newline appended.
    // The default implementation provided relies on the 'print()' method.
    virtual void printF(const char * fmt, ...) CFG_PRINTF_FUNC(2, 3);

    // Pushes a character into the line input buffer or handles a special key.
    // Returns true if the key event was handled, false if it was ignored.
    virtual bool handleKeyInput(int key, char chr);

    // Clears the terminal's screen.
    // Note: Be sure to call it even if overridden in a derived class.
    virtual void clear();

    // Call update after running a batch of commands with the CommandManager that is
    // associated to this terminal. This will make sure the newline marker is drawn.
    virtual void update();

    // Color printing:
    void setTextColor(const char * ansiColorCode) noexcept;
    void restoreTextColor() noexcept;

    //
    // Command/CVar managers:
    // (The terminal will not take ownership of the pointers).
    //

    CommandManager * getCommandManager()  const noexcept;
    CommandExecMode  getCommandExecMode() const noexcept;
    CVarManager    * getCVarManager()     const noexcept;

    void setCommandManager(CommandManager * cmdMgr)  noexcept;
    void setCommandExecMode(CommandExecMode newMode) noexcept;
    void setCVarManager(CVarManager * cvarMgr)       noexcept;

    //
    // Command history for the current session:
    //

    // Get the number of commands in the history.
    int getCommandHistorySize() const noexcept;

    // Get a given command for the command history.
    const char * getCommandFromHistory(int index) const;

    // Clear the current history for this session. Save file unaffected, if any.
    void clearCommandHistory();

    // Prints a list of all commands in the current history using the printF() method.
    void printCommandHistory();

    // Save/load history from file. Save filename is the 'CFG_COMMAND_HIST_FILE' macro.
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

    struct BuiltInCmd final
    {
        // Built-in commands take no arguments. Some examples are "exit", "clear", "histView", ...
        void (*handler)(SimpleCommandTerminal & term);
        const char * name;
        const char * desc;
    };

    // User can query these, but not extend.
    int getBuiltInCommandsCount() const noexcept;
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
    // If onGetClipboardString() returns null or an empty string, the terminal just ignores it.
    virtual void onSetClipboardString(const char * str);
    virtual const char * onGetClipboardString();

    //
    // Other internal helpers:
    //

    // Input line buffer / edit line:
    const char * getLineBuffer() const;
    void setLineBuffer(const char * str);
    bool isLineBufferEmpty() const;
    void newLineNoMarker();      // New line WITHOUT the marker.
    void newLineWithMarker();    // New line WITH the marker.
    void clearVisibleEditLine(); // Clears the line already in the terminal.
    void clearLineInputBuffer(); // Clears the local input buffer only.

    // Running a command after the user hits [RETURN].
    bool finishCommand();
    void execCmdLine(const char * cmd);

    // Sets the line input buffer from command history. No-op if no history.
    bool nextCmdFromHistory();
    bool prevCmdFromHistory();
    void addCmdToHistory(const char * cmd);

    // Navigate the input cursor by one char (left/right arrow keys).
    bool navigateTextLeft();
    bool navigateTextRight();
    void redrawInputLine();

    // Keyboard handling:
    bool handleCtrlKey(char chr); // Extended input handling for [CTRL]+char.
    bool tabCompletion();         // Command/CVar completion triggered by a [TAB] key.
    bool discardInput();          // Clears the input buffer and edit line after an [ESCAPE] key.
    bool popChar();               // Removes the char at the cursor position for [BACKSPACE].
    bool delChar();               // Removes the char under the cursor for [DELETE].
    bool insertChar(char chr);    // Add new printable char to the line input. Inserts at cursor pos.

    // Command/CVar auto-completion helpers:
    void listAllCommands();
    bool hasFullNameInLineBuffer() const;
    void listMatches(const char * partialStr, const char ** matches, int matchesFound,
                     int maxMatches, int maxPerLine, int spacing, bool coloredMatch);

    using FindMatchesCallback = std::function<void(SimpleCommandTerminal &, const char *, const char **, int *, int)>;
    bool displayCompletionMatches(const char * partialString, int maxPerLine, bool whitespaceAfter1Match,
                                  bool allowCyclingValues, const FindMatchesCallback & findMatchesCb);

private:

    static const BuiltInCmd builtInCmds[];                     // The built-in commands like "exit", "clear", etc. Fixed size array.
    static constexpr int MaxCompletionMatches  = 64;           // Maximum number of auto-completion matches to list for commands/CVar/args.
    static constexpr int MaxCmdMatchesPerLine  = 4;            // Max matches to list per line when doing command auto-completion.
    static constexpr int MaxCVarMatchesPerLine = 1;            // Max matches to list per line when doing CVar auto-completion.
    static constexpr int MaxArgMatchesPerLine  = 1;            // Max matches to list per line when doing argument auto-completion.
    static constexpr int CmdHistoryMaxSize     = 40;           // Max size in commands of the command history.
    static constexpr int LineBufferMaxSize     = 2048;         // Max length in chars of the line input buffer, including a '\0'.

    int               lineBufferUsed;                          // Chars of the line input buffer currently used.
    int               lineBufferInsertionPos;                  // Insertion position of the next handled char.
    int               currCmdInHistory;                        // Current command to be "viewed" in the history.
    int               indexCmdHistory;                         // Index into cmdHistory[] buffer for the next insertion. Also its size.
    bool              lineHasMarker;                           // If a new input line has a marker yet or not.
    bool              exitFlag;                                // Set by the "exit" built-in. Starts as false.
    bool              listAllCmdsOnTab;                        // If set, will list all the commands next time the user hits [TAB].
    bool              firstAutoCompletionTry;                  // When the first [TAB] is hit, we fill the completionMatches[] list. Subsequent ones cycle it.
    int               oldLineBufferUsed;                       // Line input buffer position must be saved with doing auto-completion.
    int               nextDisplayedCompletionMatch;            // Next match to display from completionMatches[]. Will cycle the list once all are displayed.
    int               partialStrLength;                        // Length of the partial string used to gather completionMatches[]. Needed for color printing.
    int               completionMatchCount;                    // Entries used in the completionMatches[] fixed-size array.
    const char     *  completionMatches[MaxCompletionMatches]; // List of command/CVar name completion matches to cycle on [TAB].
    CommandManager *  cmdManager;                              // Optional CommandManager for command execution and name completion.
    CVarManager    *  cvarManager;                             // Optional CVarManager for CVar name and value completion and var value substitution.
    CommandExecMode   cmdExecMode;                             // Execution mode for the CommandManager commands. Initially = Append (buffered).
    const std::string newlineMarkerStr;                        // This string is printed at the start of every new input line.
    char              lineBuffer[LineBufferMaxSize];           // Buffer to hold a line of input.
    std::string       cmdHistory[CmdHistoryMaxSize];           // Buffer with recent lines typed into the console.
    std::string       argStringMatches[MaxCompletionMatches];  // Additional strings for argument value completion on commands and CVars.
};

// ========================================================
// class NativeTerminal:
// ========================================================

//
// The NativeTerminal represents a native terminal for the platform,
// e.g.: the native command prompt on Linux/Unix or the Windows Console (cmd.exe).
// Input comes from stdin and output is written to stdout or stderr.
//
class NativeTerminal
    : public SimpleCommandTerminal
{
public:

    // Extended NativeTerminal interface:
    virtual ~NativeTerminal();
    virtual bool isTTY()    const = 0;
    virtual bool hasInput() const = 0;
    virtual int  getInput()       = 0;

    //
    // Factory functions:
    //

    // Creates a UnixTerminal instance that relies on the TERMIOS library.
    static NativeTerminal * createUnixTerminalInstance();

    // Creates a WindowsTerminal instance that relies on the ConIO library.
    static NativeTerminal * createWindowsTerminalInstance();

    // Free a previously allocated instance.
    static void destroyInstance(NativeTerminal * term);
};

// ========================================================
// Colored text printing on the terminal:
// ========================================================

namespace color
{

// This will check if stdout and stderr are not redirected to a file
// and if CFG_USE_ANSI_COLOR_CODES is defined to true.
bool canColorPrint() noexcept;

// ANSI color codes:
inline const char * restore() noexcept { return canColorPrint() ? "\033[0;1m"  : ""; }
inline const char * red()     noexcept { return canColorPrint() ? "\033[31;1m" : ""; }
inline const char * green()   noexcept { return canColorPrint() ? "\033[32;1m" : ""; }
inline const char * yellow()  noexcept { return canColorPrint() ? "\033[33;1m" : ""; }
inline const char * blue()    noexcept { return canColorPrint() ? "\033[34;1m" : ""; }
inline const char * magenta() noexcept { return canColorPrint() ? "\033[35;1m" : ""; }
inline const char * cyan()    noexcept { return canColorPrint() ? "\033[36;1m" : ""; }
inline const char * white()   noexcept { return canColorPrint() ? "\033[37;1m" : ""; }

} // namespace color {}

// ================================================================================================
//
//                           Additional library utilities and customization
//
// ================================================================================================

// ========================================================
// Library error handler:
// ========================================================

// First argument is the error message. Second is a user-defined pointer to data/context.
using ErrorHandlerCallback = void (*)(const char *, void *);

// Error printer used internally by the library.
// The output can be controlled via the error handler callback.
// This function can also be muted by calling 'silenceErrors(true)'.
// By default errors are enabled and printed to stderr (std::cerr).
bool errorF(const char * fmt, ...) CFG_PRINTF_FUNC(1, 2);

// Set/get the error handler that errorF() forwards its message to.
// The default handler just prints to stderr (std::cerr).
// To restore the default handler, pass null to the set() function.
void setErrorCallback(ErrorHandlerCallback errorHandler, void * userContext) noexcept;
ErrorHandlerCallback getErrorCallback() noexcept;

// Enable/disable error printing via errorF().
// Errors are initially enabled.
void silenceErrors(bool trueIfShouldSilence) noexcept;

// ========================================================
// Memory allocation/deallocation callbacks:
// ========================================================

struct MemoryAllocCallbacks final
{
    void * userContext;
    void * (*alloc)(std::size_t sizeInBytes, void * userContext);
    void   (*dealloc)(void * ptrToFree, void * userContext);
};

// Memory allocation and deallocation callbacks.
// The default ones forward to std::malloc() and std::free().
// Pass null to set() to restore the default callbacks.
void setMemoryAllocCallbacks(MemoryAllocCallbacks * memCallbacks) noexcept;
MemoryAllocCallbacks getMemoryAllocCallbacks() noexcept;

// ========================================================
// File IO Callbacks:
// ========================================================

using FileHandle = void *;

enum class FileOpenMode
{
    Read, // "rt"
    Write // "wt"
};

class FileIOCallbacks
{
public:
    virtual ~FileIOCallbacks();

    // Open the file in *text mode*, for reading or writing.
    // If open() fails, the output FileHandle is set to null, if the pointer itself wasn't null.
    virtual bool open(FileHandle * outHandle, const char * filename, FileOpenMode mode) = 0;

    // Close the previously open()ed FileHandle.
    // This operation is assumed to always succeed.
    // A null FileHandle is silently ignored.
    virtual void close(FileHandle fh) = 0;

    // True if the End Of File was reached.
    virtual bool isAtEOF(FileHandle fh) const = 0;

    // Rewind back to the beginning of the file.
    virtual void rewind(FileHandle fh) = 0;

    // Reads a string until the output buffer is filled or until
    // a newline (\n) is encountered, whichever comes first.
    virtual bool readLine(FileHandle fh, char * outBuffer, int bufferSize) = 0;

    // Write a null-terminated C-string.
    virtual bool writeString(FileHandle fh, const char * str) = 0;

    // Write a C-style format string. Max length limited to 2048-1 characters!
    virtual bool writeFormat(FileHandle fh, const char * fmt, ...) CFG_PRINTF_FUNC(3, 4) = 0;
};

// The FileIOCallbacks are used to read and write configuration files.
// The default callbacks use the standard C-IO API (FILE*/fopen/fprintf/etc).
// You can supply a custom set of callbacks to instead read or write from
// a different source (e.g.: a compressed directory, network drive, etc).
void setFileIOCallbacks(FileIOCallbacks * fileIO) noexcept;
FileIOCallbacks * getFileIOCallbacks() noexcept;

// ========================================================
// Boolean value strings:
// ========================================================

struct BoolCStr final
{
    const char * trueStr;
    const char * falseStr;
};

// These are the strings that map to a true/false boolean value, e.g. "true", "yes", "1", etc.
void setBoolStrings(const BoolCStr * strings) noexcept;
const BoolCStr * getBoolStrings() noexcept;

} // namespace cfg {}

#endif // CFG_HPP
