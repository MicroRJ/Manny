---
title: Tasks and graphs
label: Guide
description: Manny's task model, dependency graph, and incremental behavior.
---

A task describes one piece of work:

```elf
compile := {
	name = "compile main",
	command_line = "clang-cl /c main.c /Fobuild/main.obj",
	inputs = { "main.c" },
	outputs = { "build/main.obj" },
	dependencies = {},
}
```

The fields are deliberately small:

- `name` identifies the task in Manny's output.
- `command_line` is the program to execute.
- `inputs` are files read by the task.
- `outputs` are files produced by the task.
- `dependencies` are other tasks that must finish first.
- `working_directory` optionally changes where the command runs and how relative paths resolve.
- `transparent` optionally lets a task expose its dependencies directly to its dependants.

Pass one or more target tasks to `manny.build()`:

```elf
ret manny.build({
	targets = { compile },
})
```

Manny follows task references from those targets and constructs the reachable directed acyclic graph. A task becomes ready after its dependencies finish. Independent ready tasks execute concurrently. A failed task blocks its dependants without stopping unrelated work.

## Incremental builds

For tasks with outputs, Manny compares the current task against its recorded state. It considers:

- input and output timestamps;
- compiler-discovered header dependencies;
- the command line;
- the working directory;
- normalized input and output paths;
- relevant task flags.

Configuration is covered by a BLAKE3 fingerprint, so changing a compiler option can rebuild a task even when none of its source files changed.

Completed task state is appended while the build runs. If the build is interrupted, completed work remains available on the next run. A successful build compacts the records into a checksummed binary snapshot under `.manny/state` in the build root.

Use `--explain` to see why each task rebuilt or remained current.
