---
title: C and C++ projects
label: Guide
description: Turning a C or C++ project into Manny tasks.
---

`c.elf` takes a project description and returns ordinary Manny tasks. It does
not own a scheduler or incremental state; `manny.build()` still executes the
resulting graph.

```elf
math := {
	kind = "static_library",
	name = "math",
	sources = { "src/add.c", "src/multiply.c" },
	public_include_directories = { "include" },
}

app := {
	kind = "executable",
	name = "calculator",
	sources = { "src/main.c" },
	dependencies = { math },
}

project := {
	name = "calculator",
	default_target = app,
	targets = {
		math = math,
		app = app,
	},
}

tasks := manny.load("c").project_to_tasks(project)

entries := {}
entries.build = fun() ret manny.build({ targets = { tasks.default_task } })
ret entries
```

The project and its targets are plain tables. `project_to_tasks()` processes the
whole project and returns its default task plus a named task table.

The default toolchain is `clang-cl`, `lib`, `build`, and MSVC-style command-line
options. Pass options with the project when those defaults do not fit:

```elf
tasks := manny.load("c").project_to_tasks(project, {
	style = "gnu",
	compiler = "gcc",
	archiver = "ar",
	output_directory = "build/debug",
	compile_options = { "-std=c11", "-Wall", "-O0", "-g" },
	executable_suffix = ".exe",
})
```

The GNU command dialect is available for experimentation. Manny's supported
host is currently Windows x64.

Target kinds currently include `static_library`, `executable`, `test`, `run`,
and `generated_file`.

Common target fields are:

- `name`
- `sources`
- `dependencies`
- `private_include_directories`
- `public_include_directories`
- `defines`
- `public_defines`
- `compile_options`
- `public_compile_options`
- `link_options`
- `public_link_options`
- `generated_inputs`

Private fields affect only that target. Public fields propagate to targets that
depend on it.

## Generated files

A generator can be another target or a program path. Its arguments may use the
final output path directly:

```elf
codegen := {
	kind = "executable",
	name = "codegen",
	sources = { "tools/codegen.c" },
}

version_header := {
	kind = "generated_file",
	name = "generate version header",
	program = codegen,
	output = "generated/version.h",
	arguments = fun(output) {
		ret { output }
	},
}

core := {
	kind = "static_library",
	name = "core",
	sources = {
		{
			path = "src/core.c",
			generated_inputs = { version_header },
		},
	},
}
```

Every referenced target must also appear in the project's `targets` table.
Generated C or C++ sources can be placed directly in `sources`. Generated
headers belong in `generated_inputs` on the target or only on the source that
includes them.

## Running and testing

A `run` target has no outputs, so it runs whenever selected:

```elf
run := {
	kind = "run",
	name = "run calculator",
	target = app,
	arguments = { "--version" },
}

entries.run = fun() {
	ret manny.build({ targets = { tasks.targets.run } })
}
```

A `test` target builds and runs an executable:

```elf
tests := {
	kind = "test",
	name = "math_tests",
	sources = { "tests/math_tests.c" },
	dependencies = { math },
}

entries.test = fun() {
	ret manny.build({ targets = { tasks.targets.tests } })
}
```

Add `run` and `tests` to the project's named `targets` table before lowering it.
The resulting tasks are ordinary Manny task tables and can depend on raw tasks,
or be dependencies of raw tasks.
