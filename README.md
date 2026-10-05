# Manny

> Manny builds.

[Website](https://microrj.github.io/Manny/) ·
[Download](https://github.com/MicroRJ/Manny/releases/tag/v0.3.0-dev) ·
[Why Manny?](https://microrj.github.io/Manny/#why)

Manny is a small hackable build system for C and C++.

Build scripts are ordinary [elf](https://github.com/MicroRJ/elf) programs. They
generate task descriptions, and Manny turns the selected tasks into a graph,
executes independent work in parallel, tracks compiler-discovered dependencies,
and explains why something rebuilt.

The graph executor is also a C library. The elf frontend is one way to drive
Manny; it is not the architecture Manny is trapped inside.

Manny is currently in early development. The supported host is Windows x64, and
the public API and build-state format may still change.

## A build

```elf
elf.fs.create_directories("build")

hello := {
	name = "hello",
	command_line = "clang-cl main.c /Febuild/hello.exe",
	inputs = { "main.c" },
	outputs = { "build/hello.exe" },
}

entries := {}

entries.build = fun() {
	ret manny.build({ targets = { hello } })
}

ret entries
```

Save that as `build.elf`, then run Manny:

```text
> manny
[1/1 succeeded] hello

> manny --explain
[explain] hello: inputs are not newer than outputs
[1/1 up-to-date] hello
```

`c.elf` is an optional helper written in elf. It generates ordinary Manny task
tables; builds can modify it, replace it, or describe raw tasks directly.

## Getting started

The included example requires Visual Studio C++ build tools and `clang-cl` on
the command line.

Download and extract the latest
[Windows preview](https://github.com/MicroRJ/Manny/releases/tag/v0.3.0-dev).
Open a command prompt in its `hello` directory, then run:

```bat
..\manny.exe
..\manny.exe run
..\manny.exe --explain
```

See the [step-by-step guide](https://microrj.github.io/Manny/#step-by-step-guide)
for building Manny itself.

## Documentation

- [What](https://microrj.github.io/Manny/#what)
- [Why](https://microrj.github.io/Manny/#why)
- [How](https://microrj.github.io/Manny/#how)
- [Step-by-step guide](https://microrj.github.io/Manny/#step-by-step-guide)

The canonical sources live under [`docs`](docs).

## Build from source

```bat
git clone --recursive https://github.com/MicroRJ/Manny.git
cd Manny
build.bat
```

The checked-in blessed executable bootstraps the build. The new executable is
written to `build\manny.exe`.

## Current limitations

- Only the Windows platform implementation is complete.
- Linux support is planned but not yet implemented.
- MSVC dependency handling is not complete; Clang, clang-cl, and GCC-style Make
  dependency files are the mature path.
- There is no remote or content-addressed build cache yet.
- Tasks do not yet have isolated environment overrides.
- The C API and binary state format are still allowed to evolve.
