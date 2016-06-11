
# Lib CFG

[![Build Status](https://travis-ci.org/glampert/lib-cfg.svg)](https://travis-ci.org/glampert/lib-cfg)

Lib CFG - A small C++11 library for configuration file handling, [CVars](https://en.wikipedia.org/wiki/CVAR) and Commands.
Inspired by the in-game console of games like *Quake* and *DOOM*.

The whole library is contained into two source files, `cfg.hpp` and `cfg.cpp`,
so it should be easy to integrate it into other projects. There are no external
dependencies, besides the C and C++ Standard libraries. Some basic C++11 features
are required. Exceptions and RTTI are not used, so it should compile cleanly
with `-fno-exceptions` and `-fno-rtti`.

### Registering commands:

```cpp
cfg::CommandManager * cmdManager = cfg::CommandManager::createInstance();

// You can also register plain functions and member functions as command handlers!
cmdManager->registerCommand("my_cmd",
        [](const cfg::CommandArgs & args)
        {
            std::cout << "Running my_cmd with arguments:\n";
            for (auto arg : args)
            {
                std::cout << "> " << arg << "\n";
            }
        });

// Run the commands:
cmdManager->execAppend("my_cmd \"hello commands world!\" \'another arg for my_cmd.\'\n");
cmdManager->execBufferedCommands();
```

### Registering CVars:

```cpp
cfg::CVarManager * cvarManager = cfg::CVarManager::createInstance();

cfg::CVar * bVar = cvarManager->registerCVarBool("bVar", "a boolean", cfg::CVar::Flags::RangeCheck, true);
CFG_ASSERT(bVar->getBoolValue()   ==  true);
CFG_ASSERT(bVar->getStringValue() == "true");

cfg::CVar * iVar = cvarManager->registerCVarInt("iVar", "an integer", cfg::CVar::Flags::RangeCheck, 10, -10, +10);
CFG_ASSERT(iVar->getIntValue()    ==  10);
CFG_ASSERT(iVar->getStringValue() == "10");

cfg::CVar * fVar = cvarManager->registerCVarFloat("fVar", "a float", cfg::CVar::Flags::RangeCheck, 0.5, -1.0, +1.0);
CFG_ASSERT(fVar->getFloatValue()  ==  0.5);
CFG_ASSERT(fVar->getStringValue() == "0.5");
```

Check the `tests/` directory for other usage examples and test programs.

## License

This software is in the public domain. Where that dedication is not recognized,
you are granted a perpetual, irrevocable license to copy, distribute, and modify
the source code as you see fit.

Source code is provided "as is", without warranty of any kind, express or implied.
No attribution is required, but a mention about the author is appreciated.

