
// ================================================================================================
// -*- C++ -*-
// File: cmd_cvar_registration.cpp
// Author: Guilherme R. Lampert
// Created on: 11/06/16
// Brief: Testing Command and CVar registration.
// License: This source code is in the public domain.
// ================================================================================================

#include "cfg.hpp"
#include <iostream>

static void addCommands(cfg::CommandManager * cmdManager)
{
    // Member function handler with ref to object:
    struct CommandHandlerObject
    {
        void run(const cfg::CommandArgs & args) const
        {
            std::cout << "Running cmd " << args.getCommandName() << " with args:" << "\n";
            for (const char * arg : args)
            {
                std::cout << "> " << arg << "\n";
            }
        }
    } cmdHandler;
    cmdManager->registerCommand("foobar", makeMemFuncCommandHandler(&cmdHandler, &CommandHandlerObject::run));

    // Lambda handler function:
    cmdManager->registerCommand("cmd_1", [](const cfg::CommandArgs &) { std::cout << "Running cmd_1\n"; });
    cmdManager->registerCommand("cmd_2", [](const cfg::CommandArgs &) { std::cout << "Running cmd_2\n"; });
    cmdManager->registerCommand("cmd_3", [](const cfg::CommandArgs &) { std::cout << "Running cmd_3\n"; });
    cmdManager->registerCommand("cmd_4", [](const cfg::CommandArgs &) { std::cout << "Running cmd_4\n"; });

    // Run the commands:
    cmdManager->execAppend("cmd_1; cmd_2; cmd_3; cmd_4; foobar \"hello commands world!\" \'another arg for foobar cmd.\'\n");
    cmdManager->execBufferedCommands();

    //
    // Print each:
    //
    std::cout << "\n---- Registered Commands: ----\n";
    cmdManager->enumerateAllCommands(
            [](cfg::Command * cmd, void * /* userContext */)
            {
                std::cout << "Cmd: " << cmd->getName() << "\n";
                return true;
            }, nullptr);
    std::cout << "\n";
}

static void addCVars(cfg::CVarManager * cvarManager)
{
    //
    // Numbers:
    //
    cfg::CVar * bVar = cvarManager->registerCVarBool("bVar", "a boolean", cfg::CVar::Flags::RangeCheck, true);
    CFG_ASSERT(bVar->getBoolValue()   ==  true);
    CFG_ASSERT(bVar->getStringValue() == "true");

    cfg::CVar * iVar = cvarManager->registerCVarInt("iVar", "an integer", cfg::CVar::Flags::RangeCheck, 10, -10, +10);
    CFG_ASSERT(iVar->getIntValue()    ==  10);
    CFG_ASSERT(iVar->getStringValue() == "10");

    cfg::CVar * fVar = cvarManager->registerCVarFloat("fVar", "a float", cfg::CVar::Flags::RangeCheck, 0.5, -1.0, +1.0);
    CFG_ASSERT(fVar->getFloatValue()  ==  0.5);
    CFG_ASSERT(fVar->getStringValue() == "0.5");

    //
    // Bounded strings:
    //
    const char * allowedStrings[]
    {
        "string_0",
        "string_1",
        "string_2",
        "string_3",
        nullptr
    };
    cfg::CVar * sVar1 = cvarManager->registerCVarString("sVar1", "a string", cfg::CVar::Flags::RangeCheck, allowedStrings[0], allowedStrings);
    CFG_ASSERT(sVar1->getStringValue() == "string_0");

    //
    // Unbounded string:
    //
    cfg::CVar * sVar2 = cvarManager->registerCVarString("sVar2", "another string", cfg::CVar::Flags::Volatile, "1234", nullptr);
    CFG_ASSERT(sVar2->getStringValue() == "1234");
    CFG_ASSERT(sVar2->getIntValue()    ==  1234);

    //
    // Enums:
    //
    enum class ClassicCars
    {
        Camaro,
        Mustang,
        Maverick,
        Barracuda
    };
    const std::int64_t enumConstants[]
    {
        static_cast<std::int64_t>(ClassicCars::Camaro),
        static_cast<std::int64_t>(ClassicCars::Mustang),
        static_cast<std::int64_t>(ClassicCars::Maverick),
        static_cast<std::int64_t>(ClassicCars::Barracuda),
        0
    };
    const char * constNames[]
    {
        "Camaro",
        "Mustang",
        "Maverick",
        "Barracuda",
        nullptr
    };
    const auto initValue = static_cast<std::int64_t>(ClassicCars::Mustang);
    cfg::CVar * eVar = cvarManager->registerCVarEnum("eVar", "an enum", cfg::CVar::Flags::RangeCheck,
                                                     initValue, enumConstants, constNames);

    CFG_ASSERT(eVar->getIntValue() == static_cast<std::int64_t>(ClassicCars::Mustang));
    CFG_ASSERT(eVar->getStringValue() == "Mustang");

    //
    // Print each:
    //
    std::cout << "\n---- Registered CVars: ----\n";
    cvarManager->enumerateAllCVars(
            [](cfg::CVar * cvar, void * /* userContext */)
            {
                std::cout << "CVar " << cvar->getName() << " = " << cvar->getStringValue()
                          << " (" << cvar->getTypeString() << ")"
                          << "\n";
                return true;
            }, nullptr);
    std::cout << "\n";
}

int main()
{
    auto cvarManager = cfg::CVarManager::createInstance();
    auto cmdManager  = cfg::CommandManager::createInstance();

    // If a CVarManager is provided to the CommandManager, it will use
    // it to test that command names and CVar names never collide. And
    // also to perform CVar name expansion/substitution in command strings.
    cmdManager->setCVarManager(cvarManager);

    addCVars(cvarManager);
    addCommands(cmdManager);

    // All CVars and Commands are deleted when the mangers are destroyed.
    cfg::CommandManager::destroyInstance(cmdManager);
    cfg::CVarManager::destroyInstance(cvarManager);

    cfg::errorF("This is a test error message. Press any key to continue...");
    std::cin.get();
}
