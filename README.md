# Manny

> Manny builds.

[Website](https://microrj.github.io/Manny/) ·
[Download](https://github.com/MicroRJ/Manny/releases/tag/v0.3.0-dev) ·
[Why Manny?](https://microrj.github.io/Manny/why.html)

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
c ::= manny.load("c")

hello ::= c.executable({
	name = "hello",
	sources = { "main.c", "message.c" },
	private_include_directories = { "include" },
})

entries := {}

entries.build = fun() {
	ret manny.build({
		targets = { hello.task },
		options = { workers = 4 },
	})
}

ret entries
```

Save that as `build.elf`, then run Manny:

```text
> manny
[1/3 succeeded] compile hello: main.c
[2/3 succeeded] compile hello: message.c
[3/3 succeeded] link hello

> manny --explain
[explain] compile hello: main.c: inputs are not newer than outputs
[1/3 up-to-date] compile hello: main.c
[explain] compile hello: message.c: inputs are not newer than outputs
[2/3 up-to-date] compile hello: message.c
[explain] link hello: inputs are not newer than outputs
[3/3 up-to-date] link hello
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

See the [complete getting-started guide](https://microrj.github.io/Manny/getting-started.html)
for building Manny itself.

## Documentation

- [Why Manny?](https://microrj.github.io/Manny/why.html)
- [Getting started](https://microrj.github.io/Manny/getting-started.html)
- [Tasks and graphs](https://microrj.github.io/Manny/tasks.html)
- [C and C++ targets](https://microrj.github.io/Manny/c.html)
- [Command line](https://microrj.github.io/Manny/command-line.html)
- [Embedding Manny](https://microrj.github.io/Manny/embedding.html)

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
