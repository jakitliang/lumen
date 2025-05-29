# Lumen

![lumen](etc/lumen.png)

> Light the way of Lua.

## Intro

**Lumen** is a modernized and refined version of **Lua**, based on and fully compatible with Lua 5.1.

Designed for developers who love Lua, Lumen offers a modern, maintainable, and system-adaptive reimplementation for today's environments.

## Features

- **Full Compatibility**: Fully compatible with Lua 5.1 scripts and C libraries.
- **Performance Optimizations**: Includes multiple low-level optimizations to improve runtime efficiency, suitable for high-performance applications.
- **Ease of Use**: Retains the simplicity, readability, and easy integration of the original Lua 5.1.
- **Extensibility**: Offers a richer API surface, making it easier for developers to extend and customize Lua.
- **Lightweight**: A compact and efficient core that stays true to Lua’s lightweight philosophy.
- **Modernization**: Modernized internal architecture to better suit today's systems and development needs.

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

- Some of Lua 5.2, 5.3 APIs and Aux APIs are added
- Bitwise library is included by default.
- Modern C++ APis are added

## License

Copyright (c) 2025 Jakit Liang

This project is licensed under the BSD-2 Clause License. See the LICENSE file for more details.
