
// ================================================================================================
// -*- C++ -*-
// File: native_terminal.cpp
// Author: Guilherme R. Lampert
// Created on: 10/06/16
// Brief: Minimal test application for the NativeTerminal.
// License: This source code is in the public domain.
// ================================================================================================

#include "cfg.hpp"

int main(const int argc, const char * argv[])
{
    using namespace cfg;

    auto cvarManager = CVarManager::createInstance();
    auto cmdManager  = CommandManager::createInstance();

    #if defined(CFG_BUILD_UNIX_TERMINAL)
    auto terminal = NativeTerminal::createUnixTerminalInstance();
    #elif defined(CFG_BUILD_WIN_TERMINAL)
    auto terminal = NativeTerminal::createWindowsTerminalInstance();
    #endif // Apple/Win/Linux

    cmdManager->setCVarManager(cvarManager);
    terminal->setCVarManager(cvarManager);
    terminal->setCommandManager(cmdManager);

    registerDefaultCommands(cmdManager, terminal);
    cmdManager->execStartupCommandLine(argc, argv);

    while (terminal->isTTY() && !terminal->exit())
    {
        if (cmdManager->hasBufferedCommands())
        {
            cmdManager->execBufferedCommands();
            continue; // Inhibit console input while processing commands.
        }

        terminal->update();

        if (terminal->hasInput())
        {
            const int keyCode = terminal->getInput();

            // Upper 24bits are a SpecialKeys constant or zero.
            // Lower 8bits are an ASCII char or zero.
            terminal->handleKeyInput(keyCode & 0xFFFFFF00, keyCode & 0xFF);
        }
    }

    // Destroys any registered CVars and Commands.
    NativeTerminal::destroyInstance(terminal);
    CommandManager::destroyInstance(cmdManager);
    CVarManager::destroyInstance(cvarManager);
}
