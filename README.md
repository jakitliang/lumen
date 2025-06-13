# Lumen

![lumen](etc/lumen.png)

> Light the way of Lua.

## Intro

**Lumen** is a modernized reinvention of **Lua**, based on and fully compatible with Lua 5.1.

Designed for developers who love Lua, Lumen offers a state-of-the-art reimplementation for today's OS environments.

## Features

- **Full Compatibility**: Fully compatible with Lua 5.1 and C / C++ libraries
- **Performance Optimizations**: Some low-level optimizations done for the efficiency of the runtime
- **Easy to use**: Keeps the simplicity and readability
- **Extensibility**: Offers richer APIs, making it easier for developers to extend and customize
- **Lightweight**: Keeps the implementation of core tiny and efficient
- **Modernization**: A new designed architecture that suit for today's OSs and compilers.

## Architecture

### Kernel

> Lumen is the core provides VM and runtime libraries.

The [/lib/lumen](/lib/lumen) directory contains the **core implementation** of the `Lumen`.

And [/lib/lua](/lib/lua) provides the **standard libraries** and **extensions**,
such as `string`, `table`, `math` and etc.

### Interpreter

> Light is the main program as the entry of Lumen.

The [/src/light](/src/light) is the implementation for the Interpreter entry of `Lumen` (Lua)

`light` is the generic entry but commandline entry for Windows.

`lightw` is the graphical entry. (Windows only)

### Compiler

The [/src/lightc](/src/lightc) is the implementation for the Compiler entry of `Lumen`.

`lightc` is used to compile sources (`.lua`) into bytecode (`.luac`).

## Extensions

- Some of Lua 5.2, 5.3 APIs and Aux APIs are included
- Bitwise library is included by default
- The new and modernized APIs are base on C++17. See [lua.hpp](./include/lua.hpp).

## License

Copyright (c) 2025 Jakit Liang

This project is licensed under the BSD-2 Clause License. See the LICENSE file for more details.
