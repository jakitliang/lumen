# Lumen

![lumen](etc/lumen.png)

> Light the way of Lua.

## Intro

**Lumen** is a modernized reinvention of **Lua**.

Designed for developers who love Lua, Lumen offers a state-of-the-art reimplementation for today's OS environments.

## Features

- **Full Compatibility**: Fully compatible with Lua 5.1 and C / C++ libraries
- **Performance Optimizations**: Some low-level optimizations done for the efficiency of the runtime
- **Easy to use**: Keeps the simplicity and readability
- **Extensibility**: Offers richer APIs over 5.2 and 5.3, making it easier for developers to extend and customize
- **Lightweight**: Keeps the implementation of core tiny and efficient
- **Modernization**: A new designed architecture that suit for today's OSs and compilers.

## Lumen Native Interface

> `LNI` is a handle-based native interface for Lumen, inspired by JNI of JVM.

Lumen Native Interface, aka `LNI` allows developers to interact with Lumen's VM in a safe, modern, and efficient way using C++17, providing handle-based access to Lua objects, tables, strings, and user data.

It is designed for:

- Embedding Lumen into C++ projects with clear and safe APIs
- Extending Lumen VM with native libraries efficiently
- Building high-performance systems while maintaining VM memory safety

See [lumen.h](./include/lumen.h) for the API interfaces.

### Comparison to others

| Feature                  | Lua API                   | Lumen LNI              | Java JNI                   | Python C API                    | Ruby C API       |
| ------------------------ | ------------------------- | ---------------------- |----------------------------| ------------------------------- | ---------------- |
| **Interface Type**       | Stack-based               | Handle-based           | Handle-based               | Handle-based                    | Handle-based     |
| **Object Access**        | No direct object exposure | Direct handle exposure | Exposes `jobject`          | Exposes `PyObject*`             | Exposes `VALUE`  |
| **Cross-function Usage** | ❌ Inconvenient            | ✅ Supported            | ✅ Supported                | ✅ Supported                     | ✅ Supported      |
| **Lifecycle Management** | Managed by VM             | Managed by VM          | VM + explicit ref strength | Reference counting              | Managed by VM GC |
| **GC Integration**       | Automatic                 | Automatic              | Automatic                  | Automatic (ref counting)        | Automatic        |
| **Complexity**           | Simple                    | Simple                 | Medium (verbose)           | Medium (requires INCREF/DECREF) | Simple           |


## Projects

| Name          | Lua Version      | Usage case            |
|---------------|------------------|-----------------------|
| ChocoLight    | Lua 5.1 / LuaJIT | Already inside        |
| cocos2d-x     | Lua 5.1          | As plugin             |
| Love2D        | Lua 5.1 / LuaJIT | As plugin             |
| Unity         | Lua 5.1          | As plugin             |
| Unreal Engine | Lua 5.1          | As plugin             |
| Godot         | Lua 5.1          | As plugin             |
| Neovim        | Lua 5.1 / LuaJIT | As replacement plugin |
| Redis         | Lua 5.1          | As replacement plugin |
| nginx-lua     | Lua 5.1          | As replacement plugin |

## Architecture

### Runtime

> Lumen is the core provides VM and runtime libraries.

The [/lib/lumen](/lib/lumen) directory contains the **core implementation** of the `Lumen`.

And [/lib/lua](/lib/lua) provides the **libraries** and **extensions**,
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

- Most of Lua 5.2, 5.3 APIs and Aux APIs are inside
- Bitwise library is included by default
- UTF8 library is inside

## License

Copyright (c) 2025 Jakit Liang

This project is licensed under the BSD-2 Clause License. See the LICENSE file for more details.
