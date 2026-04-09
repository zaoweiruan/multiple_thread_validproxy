---
name: cmake-build
description: Build and test C++ CMake project with Ninja
license: MIT
compatibility: opencode
metadata:
  audience: developers
  workflow: build
---
## What I do
- Build CMake project using Ninja generator
- Run validproxy.exe from project root
- Execute tests with ctest

## When to use me
Use this when you need to compile or test the C++ proxy validation tool.

## Build Commands
```bash
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --parallel 8
./validproxy.exe
ctest -V
```