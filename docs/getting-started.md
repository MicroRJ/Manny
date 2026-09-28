---
title: Getting started
label: Guide
description: Download, build, and run Manny on Windows.
---

Manny currently supports Windows x64.

The included C example requires:

- Visual Studio C++ build tools;
- `clang-cl` available from the command line.

Download and extract the latest [Windows preview](https://github.com/MicroRJ/Manny/releases/tag/v0.3.0-dev). Open a command prompt in its `hello` directory, then run:

```bat
..\manny.exe
..\manny.exe run
..\manny.exe --explain
```

The first command builds the example. The second runs it. The third reports why each task rebuilds or remains current.

## Build Manny from source

Git with submodule support is required:

```bat
git clone --recursive https://github.com/MicroRJ/Manny.git
cd Manny
build.bat
```

The checked-in blessed executable bootstraps the build. The new executable is written to `build\manny.exe`.

Try the source-tree example:

```bat
cd example
..\build\manny.exe
..\build\manny.exe run
..\build\manny.exe --explain
```

Generated files live under `example\build`, and persistent incremental state lives under `example\.manny`.
