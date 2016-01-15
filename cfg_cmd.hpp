
// ================================================================================================
// -*- C++ -*-
// File: cfg_cmd.hpp
// Author: Guilherme R. Lampert
// Created on: 04/11/15
// Brief: Declarations for the Command classes, CommandManager and related interfaces.
// ================================================================================================

#ifndef CFG_CMD_HPP
#define CFG_CMD_HPP

#include "cfg_utils.hpp"

#if CFG_CMD_WANTS_STD_FUNCTION_DELEGATES
#include <functional>
#endif // CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

namespace cfg
{

// ========================================================
// Constants related to the Command System:
// ========================================================

// Maximum length in characters of a command name, including a null terminator.
static const int MaxCommandNameLength   = 32;

// Max length in characters of the description comment the user can attach to a command.
static const int MaxCommandDescLength   = 100;

// Maximum length in characters of a string of arguments, including null terminator.
static const int MaxCommandArgStrLength = 2048;

// Maximum number of arguments for a single command.
static const int MaxCommandArguments    = 64;

// Max length in chars of the command buffer used by CommandManager, including a '\0' at the end.
static const int CommandBufferSize      = 65535;

// If this many commands are executed in a single execBufferedCommands(), there's probably a reentrant loop.
static const int MaxReentrantCommands   = 999999;

// Character assumed to be the separator between different commands on the same line.
static const int CommandTextSeparator   = ';';

// ========================================================
// class CommandArgs:
// ========================================================

class CommandArgs CFG_FINAL_CLASS
{
public:

    // Construct from a string of arguments separated by whitespace.
    // The first argument extracted is assumed to be the command/program
    // name and will be available via `getCommandName()` but won't be included
    // in the argStrings. The command string may be preceded and followed by any
    // number of whitespace chars. Sequences of quoted characters are assumed
    // to be strings and are not split by whitespace, producing one argument.
    // Single and double quotes are treated the same way.
    explicit CommandArgs(const char * cmdStr);

    // Construct from a pre-split argc/argv array of strings, Unix-style.
    // This can be used to copy the command line arguments from the `main()` function.
    // First entry is assumed to be the program name; `argc` is expected to be >= 1.
    CommandArgs(int argc, const char ** argv);

    // Copy/assignment:
    CommandArgs(const CommandArgs & other);
    CommandArgs & operator = (const CommandArgs & other);

    // Number of argument strings parsed, NOT including the command/program name.
    int getArgCount() const;

    // Get the individual argument strings:
    const char * operator[](int index) const;
    const char * getArgAt(int index) const;
    const char * getCommandName() const;

private:

    // Helpers:
    void parseArgString(const char * argStr);
    bool addArgString(const char * argStr);
    const char * appendToken(const char * token, int tokenLen);

    // Number of arguments parsed and inserted in the `argStrings[]` array.
    int argCount;

    // Index to the start of the next free slot in `tokenizedArgStr[]`.
    // Only used during command line parsing.
    int nextTokenIndex;

    // Array of arguments. Each one points to a part of `tokenizedArgStr[]`.
    const char * argStrings[MaxCommandArguments];

    // Extra pointer to the command/program name, which is assumed to be
    // the first string of `tokenizedArgStr[]`. argStrings will refer to the
    // parameters that follow the command name but won't include this name.
    const char * cmdName;

    // Tokenized string of arguments. Each arg is separated by a null char.
    // Each entry in `argStrings[]` points to a section of this buffer.
    char tokenizedArgStr[MaxCommandArgStrLength];
};

// ========================================================
// Helper typedefs:
// ========================================================

//
// User defined integer bitflags for the commands.
// Can be used to flags commands for search or to disable specific commands.
//
typedef int CommandFlags;

//
// The "Callback" interface uses plain function pointers.
// We also provide specializations for member functions.
//
typedef void (*CommandHandlerCallback)(const CommandArgs &);
typedef int  (*CommandArgCompletionCallback)(const char *, int, const char **, int);

//
// "Delegates" are the generic std::function interface, which can bind
// to anything from lambdas to member functions. This interface is optional
// so you can still build the library in C++98 compatibility mode.
//
#if CFG_CMD_WANTS_STD_FUNCTION_DELEGATES
typedef std::function<void(const CommandArgs &)> CommandHandlerDelegate;
typedef std::function<int(const char *, int, const char **, int)> CommandArgCompletionDelegate;
#endif // CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

// ========================================================
// class CommandHandler:
// ========================================================

class CommandHandler
    : public HashTableLink<CommandHandler>
{
    CFG_DISABLE_COPY_ASSIGN(CommandHandler);

public:

    // Construct with common parameters. Description may be null or an empty string.
    // Min and max args may be negative to disable CommandManager-side argument count
    // validation. Flags are any user defined set of integer bitflags, including zero.
    CommandHandler(const char * cmdName,
                   const char * cmdDesc  = "",
                   CommandFlags cmdFlags =  0,
                   int minCmdArgs        = -1,
                   int maxCmdArgs        = -1,
                   bool managerOwnsIt    = false);

    // User defined command execution handler. Default impl is a no-op.
    virtual void onExecute(const CommandArgs & args);

    // Called with a partial list of command arguments to provide argument
    // completion. Useful for use in a developer terminal/console.
    virtual int onArgumentCompletion(const char *  argStr,  int argIndex,
                                     const char ** matches, int maxMatches);

    // The default command handler class has no dynamic resources to manage.
    // Empty virtual destructor provided to comply with inheritance rules.
    virtual ~CommandHandler();

    //
    // Accessors:
    //

    CommandFlags getFlags() const { return flags; }
    void setFlags(const CommandFlags newFlags) { flags = newFlags; }

    const char * getName() const { return name; }
    const char * getDesc() const { return desc; }

    int getMinArgs() const { return minArgs; }
    int getMaxArgs() const { return maxArgs; }

    bool isOwnedByCommandManager() const { return ownedByManager; }

private:

    // Opaque user defined bitflags. Can be changed after construction.
    CommandFlags flags;

    // We don't need the full range of an integer for the arg counts.
    // A command will have at most half a dozen args, in the rare case.
    const short minArgs;
    const short maxArgs;

    // If set on construction, the command manager owns this object
    // and will delete it when it is unregistered. This flag should only
    // be set for commands that are dynamically allocated with operator new.
    const bool ownedByManager;

    // A name must always be provided. Description may be an empty string.
    char name[MaxCommandNameLength];
    char desc[MaxCommandDescLength];
};

// ========================================================
// class CommandManager:
// ========================================================

class CommandManager CFG_FINAL_CLASS
{
    CFG_DISABLE_COPY_ASSIGN(CommandManager);

// TODO:
//
// - CVar ref substitutions: $(var)
// - Finish commenting this interface
// - Unit Tests
//
//
// Also, Doom-style command/CVar syntax, where if a command name is not matched in
// the console, then try a CVar. E.g.:
//
// r_screenWidth 1024
//
// ^ Not a command, just setting a CVar. Better than having to type the longer
//
// set r_screenWidth 1024
//
//
// Built-in commands are important. Need to define a couple defaults, specially for CVars...
//

public:

    // Pass this constant to `execBufferedCommands()` to indicate
    // all the available commands should be processed.
    static const int ExecAll = -1;

    // Pass this constant to `disableCommandsWithFlags()` to indicate
    // all commands, independent of user flags, should be disabled.
    // Any further command text input will then be ignored.
    static const int DisableAll = -1;

    // Command execution modes for CommandManager::execute() and friends.
    // See also CommandManager execNow, execInsert and execAppend.
    enum CommandExecMode
    {
        ExecImmediate, // Immediate execution. Doesn't return until completed.
        ExecInsert,    // Insert at current position (front) of the command buffer, but don't run yet.
        ExecAppend     // Append to end of the command buffer for future execution by execBufferedCommands().
    };

    CommandManager();
    explicit CommandManager(int hashTableSize);

    ~CommandManager();

    //
    // Command registering / querying:
    //

    CommandHandler * findCommand(const char * name) const;

    // returns the total num of matches found, which might be > than maxMatches
    int findCommandsWithPartialName(const char * partialName, CommandHandler ** matches, int maxMatches) const;
    int findCommandsWithFlags(CommandFlags flags, CommandHandler ** matches, int maxMatches) const;

    bool registerCommand(CommandHandler * handler);

    bool registerCommand(const char * name,
                         CommandHandlerCallback handler,
                         CommandArgCompletionCallback completionHandler = CFG_NULL,
                         const char * description = "",
                         CommandFlags flags       =  0,
                         int minArgs              = -1,
                         int maxArgs              = -1);

    #if CFG_CMD_WANTS_STD_FUNCTION_DELEGATES
    bool registerCommand(const char * name,
                         CommandHandlerDelegate handler,
                         CommandArgCompletionDelegate completionHandler = CFG_NULL,
                         const char * description = "",
                         CommandFlags flags       =  0,
                         int minArgs              = -1,
                         int maxArgs              = -1);
    #endif // CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

    template<class ObjType>
    bool registerCommand(const char * name,
                         ObjType * objRef,
                         void (ObjType::*onExecCb)(const CommandArgs &),
                         int  (ObjType::*onArgCompletionCb)(const char *, int, const char **, int) = CFG_NULL,
                         const char * description = "",
                         const CommandFlags flags =  0,
                         const int minArgs        = -1,
                         const int maxArgs        = -1)
    {
        // Need at least an execution handler (plus the object it belongs to).
        CFG_ASSERT(objRef   != CFG_NULL);
        CFG_ASSERT(onExecCb != CFG_NULL);

        if (!registerCmdPreValidate(name))
        {
            return false;
        }

        CommandHandler * newCmd = CFG_NEW CommandHandlerImpl_ClassMethods<ObjType>(name,
                                          description, flags, minArgs, maxArgs, true,
                                          objRef, onExecCb, onArgCompletionCb);

        registeredCommands.linkWithKey(newCmd, name);
        return true;
    }

    template<class Func>
    void enumerateAllCommands(Func fn)
    {
        for (CommandHandler * cmd = registeredCommands.getFirst();
             cmd != CFG_NULL; cmd = cmd->getNext())
        {
            fn(*cmd);
        }
    }

    bool removeCommand(const char * name);
    bool removeCommand(CommandHandler * handler);

    int getRegisteredCount() const;

    // Command aliases:
    //
    // alias <name> <string> <optional description>
    //
    // alias  d1  "demomap idlog.cin ; set nextserver d2"
    //
    // alias  foo  bar
    //
    // quotes only needed for the string if it has spaces.
    // name must not be quoted (same rules as a command name).
    //
    bool createCommandAlias(const char * aliasName,
                            const char * cmdStr,
                            CommandExecMode execMode,
                            const char * description = "");

    //
    // Command text execution:
    //

    // Executes the given command text immediately.
    // Text will not be added to the command buffer.
    void execNow(const char * str);

    // Inserts the command text at the front of the command buffer (prepends it).
    // Text will not be executed nor validated until a future `execBufferedCommands()` call.
    void execInsert(const char * str);

    // Appends the command text to the end of the command buffer.
    // Text will not be executed nor validated until a future `execBufferedCommands()` call.
    void execAppend(const char * str);

    // Execute a command string with any of the available modes as a constant flag.
    void execute(CommandExecMode execMode, const char * str);

    bool hasBufferedCommands() const;
    int execBufferedCommands(int maxCommandsToExec = ExecAll);

    //
    // Miscellaneous:
    //

    void disableCommandsWithFlags(CommandFlags flags);
    void enableAllCommands();

    // Tests if a string complies with the command naming rules.
    // It DOES NOT check if the command is already registered.
    static bool isValidCommandName(const char * name);

private:

    //
    // Inner class CommandHandlerImpl_Callbacks
    //
    // Wraps a pair of C-style function pointers (callbacks) into a
    // CommandHandler that we can insert in the commands hash-table.
    //
    class CommandHandlerImpl_Callbacks CFG_FINAL_CLASS
        : public CommandHandler
    {
    public:
        CommandHandlerImpl_Callbacks(const char * cmdName,
                                     const char * cmdDesc,
                                     const CommandFlags cmdFlags,
                                     const int minCmdArgs,
                                     const int maxCmdArgs,
                                     const bool managerOwnsIt,
                                     CommandHandlerCallback execCb,
                                     CommandArgCompletionCallback argComplCb);

        void onExecute(const CommandArgs & args) CFG_OVERRIDE;
        int onArgumentCompletion(const char *  argStr,  const int argIndex,
                                 const char ** matches, const int maxMatches) CFG_OVERRIDE;

    private:
        CommandHandlerCallback execCallback;
        CommandArgCompletionCallback argCompletionCallback;
    };

    #if CFG_CMD_WANTS_STD_FUNCTION_DELEGATES
    //
    // Inner class CommandHandlerImpl_Delegates
    //
    // Wraps a pair of std::functions (delegates) into a
    // CommandHandler that we can insert in the commands hash-table.
    //
    class CommandHandlerImpl_Delegates CFG_FINAL_CLASS
        : public CommandHandler
    {
    public:
        CommandHandlerImpl_Delegates(const char * cmdName,
                                     const char * cmdDesc,
                                     const CommandFlags cmdFlags,
                                     const int minCmdArgs,
                                     const int maxCmdArgs,
                                     const bool managerOwnsIt,
                                     CommandHandlerDelegate execDl,
                                     CommandArgCompletionDelegate argComplDl);

        void onExecute(const CommandArgs & args) CFG_OVERRIDE;
        int onArgumentCompletion(const char *  argStr,  const int argIndex,
                                 const char ** matches, const int maxMatches) CFG_OVERRIDE;

    private:
        CommandHandlerDelegate execDelegate;
        CommandArgCompletionDelegate argCompletionDelegate;
    };
    #endif // CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

    //
    // Inner class CommandHandlerImpl_Alias
    //
    // Helper used to define command aliases using the same
    // CommandHandler interface. It just stores a copy of the
    // aliased command string. onExecute() pushes this string
    // into the CommandHandler buffer.
    //
    class CommandHandlerImpl_Alias CFG_FINAL_CLASS
        : public CommandHandler
    {
    public:
        CommandHandlerImpl_Alias(const char * cmdName,
                                 const char * cmdDesc,
                                 const char * cmdStr,
                                 const CommandExecMode cmdExec,
                                 CommandManager & cmdMgr,
                                 const bool managerOwnsIt);

        void onExecute(const CommandArgs & args) CFG_OVERRIDE;
        ~CommandHandlerImpl_Alias();

    private:
        const CommandExecMode execMode;
        CommandManager & manager;
        char * targetCommand;
    };

    //
    // Inner class CommandHandlerImpl_ClassMethods (template)
    //
    // Wraps a pair of class methods plus a pointer to the
    // owning object (the "this pointer") allowing a standard
    // C++ object registering a member method as a command handler.
    //
    template<class ObjType>
    class CommandHandlerImpl_ClassMethods CFG_FINAL_CLASS
        : public CommandHandler
    {
    public:
        typedef void (ObjType::*OnExecCb)(const CommandArgs &);
        typedef int  (ObjType::*OnArgCompletionCb)(const char *, int, const char **, int);

        CommandHandlerImpl_ClassMethods(const char * cmdName,
                                        const char * cmdDesc,
                                        const CommandFlags cmdFlags,
                                        const int minCmdArgs,
                                        const int maxCmdArgs,
                                        const bool managerOwnsIt,
                                        ObjType * object,
                                        OnExecCb execCb,
                                        OnArgCompletionCb argComplCb)
            : CommandHandler(cmdName, cmdDesc, cmdFlags, minCmdArgs, maxCmdArgs, managerOwnsIt)
            , objRef(object)
            , execCallback(execCb)
            , argCompletionCallback(argComplCb)
        {
        }

        void onExecute(const CommandArgs & args) CFG_OVERRIDE
        {
            if (execCallback != CFG_NULL)
            {
                CFG_ASSERT(objRef != CFG_NULL);
                (objRef->*execCallback)(args);
            }
        }

        int onArgumentCompletion(const char *  argStr,  const int argIndex,
                                 const char ** matches, const int maxMatches) CFG_OVERRIDE
        {
            if (argCompletionCallback != CFG_NULL)
            {
                CFG_ASSERT(objRef != CFG_NULL);
                return (objRef->*argCompletionCallback)(argStr, argIndex, matches, maxMatches);
            }
            return 0;
        }

    private:
        ObjType * objRef;
        OnExecCb execCallback;
        OnArgCompletionCb argCompletionCallback;
    };

    //
    // Hash function used to lookup command names in the HT.
    // Can be case-sensitive or not.
    //
    #if CFG_CMD_CASE_SENSITIVE_NAMES
    typedef StringHasher CommandNameHasher;
    #else // !CFG_CMD_CASE_SENSITIVE_NAMES
    typedef StringHasherNoCase CommandNameHasher;
    #endif // CFG_CMD_CASE_SENSITIVE_NAMES

private:

    // Misc helpers:
    void execTokenized(const CommandArgs & cmdArgs);
    bool registerCmdPreValidate(const char * cmdName) const;
    int extractNextCommand(const char *& str, char * destBuf,
                           int destSizeInChars, bool & overflowed);

    // All the available commands in a hash-table for fast lookup by name.
    LinkedHashTable<CommandHandler, CommandNameHasher> registeredCommands;

    // Commands matching this set of flags will not be allowed to execute.
    // Zero allows execution of all commands. DisableAll/-1 prevents all commands from executing.
    CommandFlags disabledCmdFlags;

    // Chars used so far in the cmdBuffer. Up to CommandBufferSize-1
    // since the max size must also accommodate a null terminator at the end.
    int cmdBufferUsed;

    // Buffered commands. Huge string with individual
    // command+args blocks separated by a semicolon (;).
    char cmdBuffer[CommandBufferSize];
};

} // namespace cfg {}

#endif // CFG_CMD_HPP
