# What

Manny is an all-in-one, radically minimal build system for C and C++ projects.

The core interface is one single function: `manny.build(...)`. It takes a plain
list of target tasks and handles the rest.

What makes Manny powerful is that you can use the script to generate tasks
whichever way you want.

Manny's core remains simple while the script adapts to your project's needs.

There are three core layers:

- A general-purpose parallel graph executor.
- A tight build system built on top.
- The build system exposed to a scripting language through one function.

For the script, we use `elf`: a custom, minimal, general-purpose scripting
language. Its main data structure is the table.

# Why

I despise build systems, especially the “good” ones.

They usually fall into one of three camps:

1. An ugly, inconsistent DSL with no real power.
2. An enormous machine nobody completely understands.
3. A script that gives you freedom by making you implement the entire build
   system yourself.

And no, I did not want to write my C builds in Python.

I wanted a simple tool that would allow me to build my C projects.

Coming from Handmade Hero, I was used to the unity build approach and a batch
file. But sometimes you actually need multiple compilation units.

The problem is that existing systems are massive, cumbersome, and just plain
annoying.

Ninja forces me to still rely on some other program and a weird DSL.

CMake is massive and has an unfamiliar DSL.

Using something like Make doesn't cut it either because it has the same DSL
problem.

Meson forces me to use its project descriptors and makes custom,
general-purpose work awkward or impossible.

Virtually everything else forces me to have Python or some other thing.

Why can't I have a single, tiny executable to build my C project?

A build script should be a real program. It should have functions, loops,
tables, modules, and normal control flow. It should be able to inspect
arguments, generate files, load another project, or do something completely
unreasonable because the project requires it.

But the script should not have to schedule work, track headers, propagate
failures, maintain incremental state, or figure out what changed.

**That is Manny's job.**

Manny is a self-contained, general-purpose, programmable build system with a
bounded, well-defined core. Unlike build-system generators such as Meson and
CMake, Manny does not generate files for another tool to execute. It constructs
and runs the task graph itself.

Manny has no executable target type. It has no static-library target type. It
does not need to understand your directory layout, decide what your project
should look like, or generate files for another build system to execute.

It receives the work and builds it.

Manny handles parallel execution, compiler-discovered dependencies,
fingerprints, persistent state, interruption, failure propagation, and
explaining why something rebuilt. The script remains free to generate the graph
however it wants.

I can write my own project descriptors, scan folders, and build everything
within them. I can use regular variables, control flow, and such. I can do
arbitrary work and call arbitrary stuff. I can literally do anything because
we're not shipping a restrictive model, we're shipping a minimal library.

Generate the table however you want. Give it to Manny.

That's freaking it.

Manny is still in its early stages, but thus far, it shows great promise.

# How

A `build.elf` file is an elf program. It creates tasks as plain tables, connects
them through direct references, and passes the targets you want to build to
`manny.build()`.

```elf
elf.fs.create_directories("build")

compile := {
	name = "compile main",
	command_line = "clang-cl /c main.c /Fobuild/main.obj",
	inputs = { "main.c" },
	outputs = { "build/main.obj" },
}

link := {
	name = "link hello",
	command_line = "clang-cl build/main.obj /Febuild/hello.exe",
	inputs = { "build/main.obj" },
	outputs = { "build/hello.exe" },
	dependencies = { compile },
}

entries := {}

entries.build = fun() {
	ret manny.build({ targets = { link } })
}

ret entries
```

A task is a table. Dependencies are references to other tasks. The selected
tasks become a graph. Manny executes that graph.

Now, defining an entire project exactly like this would be fairly laborious.
This is almost like writing Ninja directly.

So instead, you can use the script itself to make it easier.

You can create your own project descriptor as plain tables, translate those to
tasks, and build your project. Use functions, loops, modules, filesystem
inspection, or whatever else the project needs. In the end, Manny only needs
the task graph.

The step-by-step guide starts with running `build.elf` and builds up from there.
