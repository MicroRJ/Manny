---
title: I despise build systems, especially the “good” ones.
label: Why Manny?
description: Why Manny exists.
---

They usually fall into one of three camps:

1. An ugly, inconsistent DSL with no real power.
2. An enormous machine nobody completely understands.
3. A script that gives you freedom by making you implement the entire fucking build system yourself.

And no, I did not want to write my C builds in Python.

What I wanted was stupidly simple.

A build script should be a real program. It should have functions, loops, tables, modules, and normal control flow. It should be able to inspect arguments, generate files, load another project, or do something completely unreasonable because the project requires it.

But the script should not have to schedule work, track headers, propagate failures, maintain incremental state, or figure out what changed.

**That is Manny's job.**

Manny is a self-contained, general-purpose, programmable build system with a bounded, well-defined core. Unlike build-system generators such as Meson and CMake, Manny does not generate files for another tool to execute. It constructs and runs the task graph itself.

**This is Manny's raw task description.**

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

That is the core interface.

A task is a table. Dependencies are references to other tasks. The selected tasks become a graph. Manny executes that graph.

Manny has no executable target type. It has no static-library target type. It does not need to understand your directory layout, decide what your project should look like, or generate files for another build system to execute.

It receives the work and builds it.

Manny handles parallel execution, compiler-discovered dependencies, fingerprints, persistent state, interruption, failure propagation, and explaining why something rebuilt. The script remains free to generate the graph however it wants.

Writing every compile and link task by hand gets old. You can write whatever abstraction fits your project, or use the C project descriptor shipped with Manny:

```elf
hello := {
	kind = "executable",
	name = "hello",
	sources = { "main.c", "message.c" },
}

project := {
	name = "hello",
	default_target = hello,
	targets = { hello = hello },
}

tasks := manny.load("c").project_to_tasks(project)

entries := {}

entries.build = fun() {
	ret manny.build({ targets = { tasks.default_task } })
}

ret entries
```

`manny.load("c")` loads `c.elf`, an ordinary elf script shipped beside Manny. `project_to_tasks()` processes the project, generates the raw compilation and link tasks, connects their dependencies, and gives the script those tasks back.

The build engine does not know that `project_to_tasks()` exists. There is no privileged C project system hiding underneath it. The helper produces the same tables I could have written by hand.

It is convenient, not mandatory. Use it, change it, replace it, or ignore it completely.

> The build script gets to be arbitrary.
>
> The build engine does not.

Generate the table however you want. Give it to Manny.

That's freaking it.

**It's just Manny.**
