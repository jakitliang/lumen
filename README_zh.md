# 流明

![lumen](etc/lumen.png)

> 照你来时路

## 简介

**流明（Lumen）** 是一条经现代化改良的 **路（Lua）**。

面向广大热爱 Lua 的开发者，Lumen 提供了一个更先进、更可维护且适配当代系统环境的全新实现。

## 特性

- **完全兼容**：与 Lua 5.1 脚本和 C 库无缝兼容
- **性能优化**：多项底层优化，提升运行效率，适合高性能应用场景
- **易用性**：保留 Lua 5.1 简洁、易学、易集成的特性
- **可扩展性**：提供 Lua 5.2, 5.3 级丰富的 API，抽象设计便于开发者扩展和定制
- **轻量级**：核心小巧高效，保持 Lua 一贯的轻量特性
- **现代化**：全新的底层架构，面向现代系统环境与开发需求

## LNI 本地接口

> `LNI` 是 Lumen 的基于句柄的本地接口，灵感来自 JVM 的 JNI。

`Lumen Native Interface` 简称 `LNI`，允许开发者使用 C++17 安全、现代、高效地与 Lumen VM 交互，
提供对 Lua 原生对象、表、字符串和用户数据的句柄式访问。

它的设计目标包括：

- 将 Lumen 安全高效地嵌入 C++ 项目
- 高性能地为 Lumen VM 扩展本地库
- 构建高性能系统的同时保持 VM 内存安全

可参考 [lumen.h](./include/lumen.h) 查看初始接口

### 参考比对

| 特性         | Lua API | Lumen LNI | Java JNI      | Python C API         | Ruby C API |
| ---------- |---------|-----------|---------------| -------------------- | ---------- |
| **接口类型**   | 栈封装式    | 句柄式       | 句柄式           | 句柄式                  | 句柄式        |
| **对象访问**   | 不直接暴露对象 | 直接暴露对象句柄  | 暴露 jobject    | 暴露 PyObject\*        | 暴露 VALUE   |
| **跨函数使用**  | 不方便     | ✅ 可跨函数使用  | ✅ 可跨函数        | ✅ 可跨函数               | ✅ 可跨函数     |
| **生命周期管理** | VM 内管理  | VM 内管理    | VM + 显式引用强度管理 | 引用计数                 | VM GC 管理   |
| **GC 集成**  | 自动      | 自动        | 自动            | 自动 (引用计数)            | 自动         |
| **复杂度**    | 简单      | 简单        | 中等 (JNI 繁琐)   | 中等 (需 INCREF/DECREF) | 简单         |

## 应用

| 项目             | Lua 版本           | 使用建议             |
|----------------|------------------|------------------|
| ChocoLight     | Lua 5.1 / LuaJIT | 自研通用引擎，内置        |
| cocos2d-x      | Lua 5.1          | 作为插件，完美兼容        |
| Love2D         | Lua 5.1 / LuaJIT | 作为插件，完美兼容        |
| Unity          | Lua 5.1          | 作为插件，完美兼容        |
| Unreal Engine  | Lua 5.1          | 作为插件，完美兼容        |
| Godot          | Lua 5.1          | 作为插件，完美兼容        |
| Neovim         | Lua 5.1 / LuaJIT | 替换为 `Lumen`，无缝兼容 |
| Redis          | Lua 5.1          | 替换为 `Lumen`，无缝兼容 |
| nginx-lua      | Lua 5.1          | 替换为 `Lumen`，无缝兼容 |

## 架构

### 运行时

> Lumen 是提供虚拟机和运行时库的核心.

首先，目录 [/lib/lumen](/lib/lumen) 包含了 `Lumen` 的核心实现.

其次，目录 [/lib/lua](/lib/lua) 包含了 **标准库** 和 **扩展库**，
比如像 `string`、`table`、`math` 之类的……


### 解释器

> Light 是 Lumen 的主程序入口.

目录 [/src/light](/src/light) 是 `Lumen` (Lua) 的解释器入口实现

`light` 是通用的入口程序，但对 Windows 来说，它是控制台程序.

`lightw` 是图形入口程序. (仅限 Windows)

### 编译器

目录 [/src/lightc](/src/lightc) 是 `Lumen` 的编译器入口实现.

`lightc` 用于编译源码 (`.lua`) 成字节码 (`.luac`).

## 扩展

- 扩充了 5.2, 5.3 的大部分 API 和 Aux API
- 自带 位运算库，且默认引入
- 自带 UTF8 标准库，默认引入
- 全新基于 C++17 标准的 API，详见 [lumen.h](./include/lumen.h)

## 版权声明

版权所有 (c) 2025 Jakit Liang

本项目采用 BSD-2 Clause License 许可协议，详情请见 LICENSE 文件。
