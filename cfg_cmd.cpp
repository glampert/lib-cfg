
// ================================================================================================
// -*- C++ -*-
// File: cfg_cmd.cpp
// Author: Guilherme R. Lampert
// Created on: 04/11/15
// Brief: Implementation of the Command System for the CFG library.
// ================================================================================================

#include "cfg_cmd.hpp"

namespace cfg
{

// ========================================================
// class CommandArgs:
// ========================================================

CommandArgs::CommandArgs(const char * cmdStr)
    : argCount(0)
    , nextTokenIndex(0)
    , cmdName(CFG_NULL)
{
    CFG_ASSERT(cmdStr != CFG_NULL);

    CFG_CLEAR_ARRAY(argStrings);
    CFG_CLEAR_ARRAY(tokenizedArgStr);

    parseArgString(cmdStr);
}

CommandArgs::CommandArgs(const int argc, const char ** argv)
    : argCount(0)
    , nextTokenIndex(0)
{
    CFG_ASSERT(argc >= 1);
    CFG_ASSERT(argv != CFG_NULL);

    CFG_CLEAR_ARRAY(argStrings);
    CFG_CLEAR_ARRAY(tokenizedArgStr);

    // Cmd/prog name
    cmdName = appendToken(argv[0], CFG_ISTRLEN(argv[0]));

    for (int i = 1; i < argc; ++i)
    {
        const char * argStr = appendToken(argv[i], CFG_ISTRLEN(argv[i]));
        if (!addArgString(argStr))
        {
            break;
        }
    }
}

CommandArgs::CommandArgs(const CommandArgs & other)
{
    (*this) = other;
}

CommandArgs & CommandArgs::operator = (const CommandArgs & other)
{
    if (this == &other)
    {
        return *this;
    }

    // Reset the current:
    argCount = 0;
    nextTokenIndex = 0;

    CFG_CLEAR_ARRAY(argStrings);
    CFG_CLEAR_ARRAY(tokenizedArgStr);

    cmdName = appendToken(other.getCommandName(),
                          CFG_ISTRLEN(other.getCommandName()));

    for (int i = 0; i < other.getArgCount(); ++i)
    {
        const char * argStr = appendToken(other.getArgAt(i),
                                          CFG_ISTRLEN(other.getArgAt(i)));

        // Don't bother checking since if all arguments fit in
        // the source CommandArgs, they must fit in here too!
        addArgString(argStr);
    }

    return *this;
}

int CommandArgs::getArgCount() const
{
    return argCount; // Not counting the command/prog name.
}

const char * CommandArgs::getArgAt(const int index) const
{
    CFG_ASSERT(index >= 0 && index < argCount);
    CFG_ASSERT(argStrings[index] != CFG_NULL);
    return argStrings[index];
}

const char * CommandArgs::operator[](const int index) const
{
    CFG_ASSERT(index >= 0 && index < argCount);
    CFG_ASSERT(argStrings[index] != CFG_NULL);
    return argStrings[index];
}

const char * CommandArgs::getCommandName() const
{
    CFG_ASSERT(cmdName != CFG_NULL);
    return cmdName; // Stored separately from the argStrings.
}

void CommandArgs::parseArgString(const char * argStr)
{
    // First argument should be the unquoted command/program name.
    // Command string might be preceded by whitespace.

    int  argLen   = 0;
    bool firstArg = true;
    bool quotes   = false;
    bool done     = false;
    const char * newArg   = CFG_NULL;
    const char * argStart = CFG_NULL;

    for (; *argStr != '\0' && !done; ++argStr)
    {
        switch (*argStr)
        {
        // Quotes:
        //  Indicate that this arg won't end until
        //  the next enclosing quote is found.
        case  '"' :
        case '\'' :
            quotes = !quotes;
            if (argStart == CFG_NULL)
            {
                argStart = argStr;
            }
            break;

        // Whitespace:
        //  Separate each individual argument of a command line
        //  if we are not inside a quoted string.
        case  ' ' :
        case '\t' :
        case '\n' :
        case '\r' :
            if (!quotes && argStart != CFG_NULL)
            {
                argLen = static_cast<int>(argStr - argStart);
                newArg = appendToken(argStart, argLen);
                argStart = CFG_NULL;
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

        // Start of a new argument or continuing an already started one.
        default :
            if (argStart == CFG_NULL)
            {
                argStart = argStr;
            }
            break;
        } // switch (*argStr)
    }

    // End reached with an open quote?
    // We don't trash the arguments completely in such case. Some parameters
    // might still be valid. Failing will be up to the command handler.
    if (quotes)
    {
        CFG_ERROR("Attention! Command string ended with open quotation block!");
        // Allow it to proceed.
    }

    // Last residual argument before the end of the string.
    if (argStart != CFG_NULL)
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

bool CommandArgs::addArgString(const char * argStr)
{
    if (argStr == CFG_NULL)
    {
        CFG_ERROR("Null token! 'tokenizedArgStr[]' depleted?");
        return false;
    }

    if (argCount == MaxCommandArguments)
    {
        CFG_ERROR("Too many arguments! Ignoring extraneous ones...");
        return false;
    }

    argStrings[argCount++] = argStr;
    return true;
}

const char * CommandArgs::appendToken(const char * token, int tokenLen)
{
    if ((nextTokenIndex + tokenLen) >= MaxCommandArgStrLength)
    {
        CFG_ERROR("Command argument string is too long! Max: %d.", MaxCommandArgStrLength);
        return CFG_NULL;
    }

    // If the token is enclosed in single or double quotes, ignore them.
    // Note that this assumes the opening AND closing quotes are present!
    char * pDest = &tokenizedArgStr[nextTokenIndex];
    if (token[0] == '"' || token[0] == '\'')
    {
        ++token;
        tokenLen -= 2;
    }
    std::memcpy(pDest, token, tokenLen);
    nextTokenIndex += (tokenLen + 1);
    pDest[tokenLen] = '\0';

    return pDest;
}

// ========================================================
// class CommandHandler:
// ========================================================

CommandHandler::CommandHandler(const char * cmdName,
                               const char * cmdDesc,
                               const CommandFlags cmdFlags,
                               const int minCmdArgs,
                               const int maxCmdArgs,
                               const bool managerOwnsIt)
    : flags(cmdFlags)
    , minArgs(static_cast<short>(minCmdArgs))
    , maxArgs(static_cast<short>(maxCmdArgs))
    , ownedByManager(managerOwnsIt)
{
    CFG_CLEAR_ARRAY(name);
    CFG_CLEAR_ARRAY(desc);

    // Must have a valid name!
    CFG_ASSERT(cmdName != CFG_NULL && *cmdName != '\0');
    CFG_ASSERT(CFG_ISTRLEN(cmdName) < CFG_ARRAY_LEN(name) && "Command name too long!");
    copyString(name, CFG_ARRAY_LEN(name), cmdName);

    // Description comment is optional.
    if (cmdDesc != CFG_NULL && *cmdDesc != '\0')
    {
        CFG_ASSERT(CFG_ISTRLEN(cmdDesc) < CFG_ARRAY_LEN(desc) && "Description string too long!");
        copyString(desc, CFG_ARRAY_LEN(desc), cmdDesc);
    }
}

void CommandHandler::onExecute(const CommandArgs & /* args */)
{
    // No-op command.
}

int CommandHandler::onArgumentCompletion(const char *  /* argStr */,  const int /* argIndex */,
                                         const char ** /* matches */, const int /* maxMatches */)
{
    return 0; // No completion available.
}

CommandHandler::~CommandHandler()
{
    // Anchors the vtable, avoiding the
    // '-Wweak-vtables' warning from Clang.
}

// ========================================================
// class CommandHandlerImpl_Callbacks:
// ========================================================

CommandManager::CommandHandlerImpl_Callbacks::CommandHandlerImpl_Callbacks(const char * cmdName,
                                                                           const char * cmdDesc,
                                                                           const CommandFlags cmdFlags,
                                                                           const int minCmdArgs,
                                                                           const int maxCmdArgs,
                                                                           const bool managerOwnsIt,
                                                                           CommandHandlerCallback execCb,
                                                                           CommandArgCompletionCallback argComplCb)
    : CommandHandler(cmdName, cmdDesc, cmdFlags, minCmdArgs, maxCmdArgs, managerOwnsIt)
    , execCallback(execCb)
    , argCompletionCallback(argComplCb)
{
}

void CommandManager::CommandHandlerImpl_Callbacks::onExecute(const CommandArgs & args)
{
    if (execCallback != CFG_NULL)
    {
        execCallback(args);
    }
}

int CommandManager::CommandHandlerImpl_Callbacks::onArgumentCompletion(const char * argStr,
                                                                       const int argIndex,
                                                                       const char ** matches,
                                                                       const int maxMatches)
{
    if (argCompletionCallback != CFG_NULL)
    {
        return argCompletionCallback(argStr, argIndex, matches, maxMatches);
    }
    return 0;
}

// ========================================================
// class CommandHandlerImpl_Delegates:
// ========================================================

#if CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

CommandManager::CommandHandlerImpl_Delegates::CommandHandlerImpl_Delegates(const char * cmdName,
                                                                           const char * cmdDesc,
                                                                           const CommandFlags cmdFlags,
                                                                           const int minCmdArgs,
                                                                           const int maxCmdArgs,
                                                                           const bool managerOwnsIt,
                                                                           CommandHandlerDelegate execDl,
                                                                           CommandArgCompletionDelegate argComplDl)
    : CommandHandler(cmdName, cmdDesc, cmdFlags, minCmdArgs, maxCmdArgs, managerOwnsIt)
    , execDelegate(std::move(execDl))
    , argCompletionDelegate(std::move(argComplDl))
{
}

void CommandManager::CommandHandlerImpl_Delegates::onExecute(const CommandArgs & args)
{
    if (execDelegate != CFG_NULL)
    {
        execDelegate(args);
    }
}

int CommandManager::CommandHandlerImpl_Delegates::onArgumentCompletion(const char * argStr,
                                                                       const int argIndex,
                                                                       const char ** matches,
                                                                       const int maxMatches)
{
    if (argCompletionDelegate != CFG_NULL)
    {
        return argCompletionDelegate(argStr, argIndex, matches, maxMatches);
    }
    return 0;
}

#endif // CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

// ========================================================
// class CommandHandlerImpl_Alias:
// ========================================================

CommandManager::CommandHandlerImpl_Alias::CommandHandlerImpl_Alias(const char * cmdName,
                                                                   const char * cmdDesc,
                                                                   const char * cmdStr,
                                                                   const CommandExecMode cmdExec,
                                                                   CommandManager & cmdMgr,
                                                                   const bool managerOwnsIt)
    : CommandHandler(cmdName, cmdDesc, 0, 0, 0, managerOwnsIt)
    , execMode(cmdExec)
    , manager(cmdMgr)
    , targetCommand(cloneString(cmdStr))
{
}

CommandManager::CommandHandlerImpl_Alias::~CommandHandlerImpl_Alias()
{
    memFree(targetCommand);
}

void CommandManager::CommandHandlerImpl_Alias::onExecute(const CommandArgs & /* args */)
{
    manager.execute(execMode, targetCommand);
}

// ========================================================
// Local helpers:
// ========================================================
namespace
{

int cmdCompNames(const char * a, const char * b, const unsigned int count = ~0u)
{
    #if CFG_CMD_CASE_SENSITIVE_NAMES
    return std::strncmp(a, b, count);
    #else // !CFG_CMD_CASE_SENSITIVE_NAMES
    return compareStringsNoCase(a, b, count);
    #endif // CFG_CMD_CASE_SENSITIVE_NAMES
}

bool cmdNameStartsWith(const char * name, const char * prefix)
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
    return cmdCompNames(name, prefix, prefixLen) == 0;
}

bool cmdSortPredicate(const CommandHandler * a, const CommandHandler * b)
{
    return cmdCompNames(a->getName(), b->getName()) < 0;
}

void cmdSortMatches(CommandHandler ** matches, const int count)
{
    std::sort(matches, matches + count, cmdSortPredicate);
}

} // namespace {}

// ========================================================
// class CommandManager:
// ========================================================

CommandManager::CommandManager()
    : disabledCmdFlags(0)
    , cmdBufferUsed(0)
{
    CFG_CLEAR_ARRAY(cmdBuffer);
}

CommandManager::CommandManager(const int hashTableSize)
    : disabledCmdFlags(0)
    , cmdBufferUsed(0)
{
    CFG_CLEAR_ARRAY(cmdBuffer);

    if (hashTableSize > 0)
    {
        registeredCommands.allocate(hashTableSize);
    }
}

CommandManager::~CommandManager()
{
    // Cleanup the memory we own. Usually just the CommandHandlerImpl_* instances.
    CommandHandler * cmd = registeredCommands.getFirst();
    while (cmd != CFG_NULL)
    {
        CommandHandler * temp = cmd->getNext();
        if (cmd->isOwnedByCommandManager())
        {
            CFG_DELETE cmd;
        }
        cmd = temp;
    }
}

CommandHandler * CommandManager::findCommand(const char * name) const
{
    CFG_ASSERT(name != CFG_NULL);
    if (*name == '\0')
    {
        return CFG_NULL;
    }
    return registeredCommands.findByKey(name);
}

int CommandManager::findCommandsWithPartialName(const char * partialName,
                                                CommandHandler ** matches,
                                                const int maxMatches) const
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
    for (CommandHandler * cmd = registeredCommands.getFirst();
         cmd != CFG_NULL; cmd = cmd->getNext())
    {
        if (cmdNameStartsWith(cmd->getName(), partialName))
        {
            if (matchesFound < maxMatches)
            {
                matches[matchesFound] = cmd;
            }
            ++matchesFound; // Keep incrementing even if matches[] is full,
                            // so the caller can know the total found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        cmdSortMatches(matches, std::min(matchesFound, maxMatches));
    }

    return matchesFound;
}

int CommandManager::findCommandsWithFlags(const CommandFlags flags,
                                          CommandHandler ** matches,
                                          const int maxMatches) const
{
    CFG_ASSERT(matches != CFG_NULL);
    CFG_ASSERT(maxMatches > 0);

    int matchesFound = 0;
    for (CommandHandler * cmd = registeredCommands.getFirst();
         cmd != CFG_NULL; cmd = cmd->getNext())
    {
        if (cmd->getFlags() & flags)
        {
            if (matchesFound < maxMatches)
            {
                matches[matchesFound] = cmd;
            }
            ++matchesFound; // Keep incrementing even if matches[] is full,
                            // so the caller can know the total found.
        }
    }

    if (matchesFound > 0)
    {
        // Output will be sorted alphabetically.
        cmdSortMatches(matches, std::min(matchesFound, maxMatches));
    }

    return matchesFound;
}

bool CommandManager::registerCmdPreValidate(const char * cmdName) const
{
    if (!isValidCommandName(cmdName))
    {
        CFG_ERROR("Bad command name '%s'! Can't register it.", cmdName);
        return false;
    }

    if (findCommand(cmdName)) // No duplicates allowed.
    {
        CFG_ERROR("Command '%s' already registered! Duplicate commands are not allowed.", cmdName);
        return false;
    }

    return true;
}

bool CommandManager::registerCommand(CommandHandler * handler)
{
    CFG_ASSERT(handler != CFG_NULL);

    const char * cmdName = handler->getName();
    if (!registerCmdPreValidate(cmdName))
    {
        return false;
    }

    registeredCommands.linkWithKey(handler, cmdName);
    return true;
}

bool CommandManager::registerCommand(const char * name,
                                     CommandHandlerCallback handler,
                                     CommandArgCompletionCallback completionHandler,
                                     const char * description,
                                     const CommandFlags flags,
                                     const int minArgs,
                                     const int maxArgs)
{
    CFG_ASSERT(handler != CFG_NULL);
    if (!registerCmdPreValidate(name))
    {
        return false;
    }

    CommandHandler * newCmd = CFG_NEW CommandHandlerImpl_Callbacks(
                                      name, description, flags, minArgs, maxArgs, true,
                                      handler, completionHandler);

    registeredCommands.linkWithKey(newCmd, name);
    return true;
}

#if CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

bool CommandManager::registerCommand(const char * name,
                                     CommandHandlerDelegate handler,
                                     CommandArgCompletionDelegate completionHandler,
                                     const char * description,
                                     const CommandFlags flags,
                                     const int minArgs,
                                     const int maxArgs)
{
    CFG_ASSERT(handler != CFG_NULL);
    if (!registerCmdPreValidate(name))
    {
        return false;
    }

    CommandHandler * newCmd = CFG_NEW CommandHandlerImpl_Delegates(
                                      name, description, flags, minArgs, maxArgs, true,
                                      std::move(handler), std::move(completionHandler));

    registeredCommands.linkWithKey(newCmd, name);
    return true;
}

#endif // CFG_CMD_WANTS_STD_FUNCTION_DELEGATES

bool CommandManager::removeCommand(const char * name)
{
    if (!isValidCommandName(name))
    {
        CFG_ERROR("'%s' is not a valid command name! Nothing to remove.", name);
        return false;
    }

    CommandHandler * cmd = registeredCommands.unlinkByKey(name);
    if (cmd == CFG_NULL)
    {
        return false; // No such command/alias.
    }

    // Free the memory if we own it (it's a CommandHandlerImpl_* instance).
    if (cmd->isOwnedByCommandManager())
    {
        CFG_DELETE cmd;
    }

    return true;
}

bool CommandManager::removeCommand(CommandHandler * handler)
{
    CFG_ASSERT(handler != CFG_NULL);
    return removeCommand(handler->getName());
}

int CommandManager::getRegisteredCount() const
{
    return registeredCommands.getSize();
}

bool CommandManager::createCommandAlias(const char * aliasName,
                                        const char * aliasedCmdStr,
                                        const CommandExecMode execMode,
                                        const char * description)
{
    if (aliasedCmdStr == CFG_NULL || *aliasedCmdStr == '\0')
    {
        CFG_ERROR("Can't create a command alias for an empty/null string!");
        return false;
    }

    if (!isValidCommandName(aliasName))
    {
        CFG_ERROR("'%s' is not a valid alias or command name!", aliasName);
        return false;
    }

    if (findCommand(aliasName))
    {
        CFG_ERROR("A command or alias named '%s' already exists!", aliasName);
        return false;
    }

    CommandHandler * newCmd = CFG_NEW CommandHandlerImpl_Alias(
                                      aliasName, description, aliasedCmdStr,
                                      execMode, *this, true);

    registeredCommands.linkWithKey(newCmd, aliasName);
    return true;
}

void CommandManager::execTokenized(const CommandArgs & cmdArgs)
{
    // Validate the name length:
    const char * cmdName = cmdArgs.getCommandName();
    if (CFG_ISTRLEN(cmdName) >= MaxCommandNameLength)
    {
        CFG_ERROR("Command name too long! Max command name length: %d.",
                  MaxCommandNameLength);
        return;
    }

    // Find the command:
    CommandHandler * cmd = findCommand(cmdName);
    if (cmd == CFG_NULL)
    {
        CFG_ERROR("Command '%s' not found!", cmdName);
        return;
    }

    // Check if this command is currently allowed to execute:
    if (disabledCmdFlags != 0)
    {
        if (disabledCmdFlags == DisableAll)
        {
            CFG_ERROR("Command execution is currently disabled!");
            return;
        }
        if (cmd->getFlags() & disabledCmdFlags)
        {
            CFG_ERROR("Command '%s' is disabled!", cmdName);
            return;
        }
    }

    // Optional min/max arguments validation, if they are not negative (zero args is OK):
    if (cmd->getMinArgs() >= 0)
    {
        if (cmdArgs.getArgCount() < cmd->getMinArgs())
        {
            CFG_ERROR("Command '%s': Not enough arguments! Expected at least %d.",
                      cmdName, cmd->getMinArgs());
            return;
        }
    }
    if (cmd->getMaxArgs() >= 0)
    {
        if (cmdArgs.getArgCount() > cmd->getMaxArgs())
        {
            CFG_ERROR("Command '%s': Too many arguments provided! Expected up to %d.",
                      cmdName, cmd->getMaxArgs());
            return;
        }
    }

    // Arguments pre-validated, call command handler:
    cmd->onExecute(cmdArgs);
}

int CommandManager::extractNextCommand(const char *& str, char * destBuf,
                                       const int destSizeInChars, bool & overflowed)
{
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
    // This simple parsing loop allows for commands generally compatible
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
    bool backslash   = false;
    bool quotes      = false;
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
            done = !backslash && !quotes;
            backslash = false;
        }
        else if (chr == '"' || chr == '\'')
        {
            // Remember if we are inside a quoted block.
            // We have to ignore CommandTextSeparators inside them.
            quotes = !quotes;
        }
        else if (chr == CommandTextSeparator)
        {
            // Default command separator breaks a command
            // string if not inside a quoted block.
            done = !quotes;
        }

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
        charsCopied--; // Truncated.
        overflowed = true;
        CFG_ERROR("Command string too long! Can't parse all arguments from it...");
    }
    else
    {
        overflowed = false;
    }

    destBuf[charsCopied] = '\0';
    return charsCopied;
}

void CommandManager::execNow(const char * str)
{
    CFG_ASSERT(str != CFG_NULL);
    if (*str == '\0')
    {
        return;
    }

    // Input string might consist of multiple commands.
    // Split it up and handle each command separately.
    bool overflowed;
    char tempBuffer[MaxCommandArgStrLength];

    while (extractNextCommand(str, tempBuffer, CFG_ARRAY_LEN(tempBuffer), overflowed) > 0)
    {
        if (overflowed)
        {
            // Malformed command line that won't fit in our buffers.
            // Discard the rest of the string.
            break;
        }

        // Tokenize the command string, separating command name and splitting the args:
        CommandArgs cmdArgs(tempBuffer);
        execTokenized(cmdArgs);
    }
}

void CommandManager::execInsert(const char * str)
{
    CFG_ASSERT(str != CFG_NULL);
    if (*str == '\0')
    {
        return;
    }

    const int strLen = CFG_ISTRLEN(str) + 1;

    // Check for buffer overflow:
    if ((cmdBufferUsed + strLen) >= CommandBufferSize)
    {
        CFG_ERROR("Buffer overflow! Command buffer depleted in CommandManager::execInsert()!");
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

void CommandManager::execAppend(const char * str)
{
    CFG_ASSERT(str != CFG_NULL);
    if (*str == '\0')
    {
        return;
    }

    const int strLen = CFG_ISTRLEN(str) + 1;

    // Check for buffer overflow:
    if ((cmdBufferUsed + strLen) >= CommandBufferSize)
    {
        CFG_ERROR("Buffer overflow! Command buffer depleted in CommandManager::execAppend()!");
        return;
    }

    // Copy the user command:
    std::memcpy(cmdBuffer + cmdBufferUsed, str, strLen - 1);
    cmdBufferUsed += strLen;

    // Separate the command strings and null terminate the buffer:
    cmdBuffer[cmdBufferUsed - 1] = CommandTextSeparator;
    cmdBuffer[cmdBufferUsed] = '\0';
}

void CommandManager::execute(const CommandExecMode execMode, const char * str)
{
    switch (execMode)
    {
    case ExecImmediate :
        execNow(str);
        break;

    case ExecInsert :
        execInsert(str);
        break;

    case ExecAppend :
        execAppend(str);
        break;

    default :
        CFG_ERROR("Invalid CommandExecMode enum value!");
        break;
    } // switch (execMode)
}

bool CommandManager::hasBufferedCommands() const
{
    return cmdBufferUsed > 0;
}

int CommandManager::execBufferedCommands(const int maxCommandsToExec)
{
    if (cmdBufferUsed == 0 || maxCommandsToExec == 0)
    {
        return 0;
    }

    int commandsExecuted = 0;
    const char * cmdBufferPtr = cmdBuffer;

    bool overflowed;
    char tempBuffer[MaxCommandArgStrLength];

    while (extractNextCommand(cmdBufferPtr, tempBuffer, CFG_ARRAY_LEN(tempBuffer), overflowed) > 0)
    {
        if (overflowed)
        {
            // Malformed command line that won't fit in our buffers.
            // Discard the rest of the command buffer and bail out.
            cmdBufferUsed = 0;
            cmdBuffer[0]  = '\0';
            CFG_ERROR("Discarding rest of command buffer due to command string overflow...");
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
            CFG_ERROR("%d commands executed in sequence! Possible reentrant loop!", commandsExecuted);
            break;
        }

        // Reached our limit for this frame?
        if (maxCommandsToExec != ExecAll && commandsExecuted == maxCommandsToExec)
        {
            break;
        }
    }

    return commandsExecuted;
}

void CommandManager::disableCommandsWithFlags(const CommandFlags flags)
{
    disabledCmdFlags = flags;
}

void CommandManager::enableAllCommands()
{
    disabledCmdFlags = 0;
}

bool CommandManager::isValidCommandName(const char * name)
{
    //
    // Command names follow the C++ variable naming rules,
    // that is, cannot start with a number, cannot contain
    // white spaces, cannot start with nor contain any special
    // characters or punctuation, except for the underscore.
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

} // namespace cfg {}
