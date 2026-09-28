# Manny

> Manny builds.

Manny is a small programmable build system for C.

Build scripts are ordinary [elf](https://github.com/MicroRJ/elf) programs. They
generate a graph of tasks, and Manny executes that graph in parallel, tracks
compiler-discovered dependencies, and explains why something rebuilt.

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

> manny
[1/3 up-to-date] compile hello: main.c
[2/3 up-to-date] compile hello: message.c
[3/3 up-to-date] link hello
```

Ask Manny why it made the current incremental decision:

```text
> manny --explain
[explain] compile hello: main.c: inputs are not newer than outputs
[1/3 up-to-date] compile hello: main.c
[explain] compile hello: message.c: inputs are not newer than outputs
[2/3 up-to-date] compile hello: message.c
[explain] link hello: inputs are not newer than outputs
[3/3 up-to-date] link hello
```

`c.elf` provides C and C++ executables, static libraries, generated files, tests,
and public/private usage requirements. It only generates regular Manny tasks;
drop down to raw task tables whenever a build needs something unusual. See the
[C target guide](docs/c.md).

`manny` runs the `build` entry by default. A script can expose any entries it
wants:

```text
manny
manny build
manny run
manny clean
manny rant_about_how_much_i_hate_build_systems
```

An entry is just an elf function. It can construct a graph, generate files,
call other scripts, remove a directory, print something stupid, or do whatever
else the build needs.

## Why Manny?

- **The build is a program.** Use functions, loops, tables, closures, string
  interpolation, and modules instead of fighting a deliberately weak DSL.
- **Tasks are plain data.** Generate them, transform them, combine subprojects,
  and pass the resulting graph to `manny.build()`.
- **Common C builds stay small.** The bundled `c.elf` module handles ordinary
  compile, archive, link, generated-source, and test plumbing.
- **Dependencies create parallelism.** Manny schedules ready tasks across worker
  threads while respecting the graph.
- **The compiler tells Manny about headers.** Manny consumes compiler-generated
  dependency files instead of trying to understand C with an include scanner.
- **Incremental decisions include configuration.** BLAKE3 fingerprints cover
  commands, working directories, normalized paths, and relevant task flags.
- **Incremental progress survives interruption.** Completed task state is
  appended as the build runs and compacted into a checksummed binary snapshot.
- **Rebuilds are explainable.** `--explain` reports why a task ran or remained
  up to date.
- **The executor is a library.** C programs can construct a generic Manny graph,
  attach callbacks to nodes, and execute it without using the build frontend.

## The model

A build task has:

- a command line;
- zero or more inputs;
- zero or more outputs;
- zero or more task dependencies;
- an optional working directory;
- optional metadata such as include directories and transparency.

Tasks form a directed acyclic graph. A task becomes ready after its dependencies
finish. Manny runs ready tasks concurrently, blocks dependents when a dependency
fails, and finishes when every reachable task is terminal.

For incremental builds, Manny normalizes and interns paths, records dependencies
reported by the compiler, fingerprints the task description, and stores the
result under `.manny/state` in the build root.

That is the core of it. Generate the task table however you want, then give it
to Manny.

## Getting started

Requirements:

- Windows x64
- Visual Studio C++ build tools
- `clang-cl` available from the command line
- Git with submodule support

Clone Manny and its dependencies:

```bat
git clone --recursive https://github.com/MicroRJ/Manny.git
cd Manny
```

Build Manny using its checked-in blessed executable:

```bat
build.bat
```

The new executable is written to `build\manny.exe`.

Try the included example:

```bat
cd example
..\build\manny.exe
..\build\manny.exe run
..\build\manny.exe --explain
```

The example's generated files live under `example\build`, and its persistent
incremental state lives under `example\.manny`.

`manny.load("c")` loads the `c.elf` shipped beside the executable. Copy the
opening example and change the source list. Projects can instead vendor a
specific `c.elf` and load it with `elf.load_file()` when they want to pin or
modify the helpers. The low-level task format remains available for custom tools
and non-C work.

## Command line

```text
manny [build-file] [entry] [options]
```

Manny uses `build.elf` and the `build` entry when neither is specified.

```text
-q, --quiet          Suppress task output.
--explain            Explain incremental decisions.
--verbose [N]        Control Manny's internal diagnostic verbosity.
--workers N          Set the worker count.
--profile            Write profiling information.
--profile-threads    Include worker-thread profiling.
--version            Print Manny and elf versions.
```

Verbosity controls Manny's own diagnostics. Task output is printed by default and
is controlled separately by `--quiet`.

## As a C library

The abstract heart of Manny is independent of command lines and files. A `Manny`
contains nodes and dependency edges. Each node has a callback, user data, and a
result. `manny_execute()` runs the graph using the requested worker count and can
report start and completion events to the host.

The build layer sits on top of that executor and adds processes, inputs,
outputs, fingerprints, compiler dependencies, and persistent state.

This separation is intentional. Manny can be used as a build-system runner, as a
graph execution library embedded in another C program, or through a different
frontend in the future.

## Current limitations

- Only the Windows platform implementation is complete.
- Linux support is planned but not yet implemented.
- MSVC dependency handling is not complete; Clang, clang-cl, and GCC-style Make
  dependency files are the mature path.
- There is no remote or content-addressed build cache yet.
- Tasks do not yet have isolated environment overrides.
- The C API and binary state format are still allowed to evolve.

## Philosophy, or: the rant

I despise build systems, especially the "good" ones.

They tend to have at least one of these problems:

1. They use an ugly, inconsistent DSL with no real power.
2. They are bloated, obscure, and difficult to understand.
3. They are merely scripts, so I have to implement the entire build system
   myself in a scripting language I do not particularly like.

Manny uses a normal, minimal, general-purpose, C-like programming language. The
script calls `manny.build()` with a table of targets. Manny takes those targets,
constructs the graph, tracks what changed, and builds it.

Generate the table however you want.

That's freaking it.

It's just Manny.
