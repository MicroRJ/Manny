---
title: C and C++ targets
label: Guide
description: Manny's optional high-level helpers for C and C++ targets.
---

`c.elf` turns target descriptions into ordinary Manny tasks. It does not own a
scheduler or incremental state; `manny.build()` still executes the resulting
graph.

Load the module shipped beside the Manny executable and configure it once:

```elf
c ::= manny.load("c")
```

`manny.load()` only exposes modules shipped with Manny. To pin or modify the
helper, vendor `c.elf` in the project and use `elf.load_file()` instead.

The default configuration uses `clang-cl`, `lib`, `build`, and MSVC-style
command-line options. Every default can be replaced:

```elf
c_module ::= manny.load("c")
c ::= c_module.configure({
	style = "gnu",
	compiler = "gcc",
	archiver = "ar",
	output_directory = "build/debug",
	compile_options = { "-std=c11", "-Wall", "-O0", "-g" },
	link_options = {},
	executable_suffix = ".exe",
})
```

The GNU command dialect is available for experimentation. Manny's supported
host is currently Windows x64.

## Targets

Build a static library and consume it:

```elf
math ::= c.static_library({
	name = "math",
	sources = { "src/add.c", "src/multiply.c" },
	public_include_directories = { "include" },
})

app ::= c.executable({
	name = "calculator",
	sources = { "src/main.c" },
	dependencies = { math },
})

entries := {}
entries.build = fun() {
	ret manny.build({ targets = { app.task } })
}
ret entries
```

A target contains its final `task`, its `output` path, and usage requirements
that propagate through `dependencies`.

The target fields are:

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

Private fields affect only the target. Public fields are also applied to targets
that depend on it.

## Generated files

A generator can be an executable target or a program path. Its arguments may be
a function so they can use the final output path without placeholders:

```elf
codegen ::= c.executable({
	name = "codegen",
	sources = { "tools/codegen.c" },
})

version_header ::= c.generated_file({
	name = "generate version header",
	program = codegen,
	output = "generated/version.h",
	arguments = fun(output) {
		ret { output }
	},
})

core ::= c.static_library({
	name = "core",
	sources = {
		{
			path = "src/core.c",
			generated_inputs = { version_header },
		},
	},
})
```

A generated C or C++ source can be placed directly in `sources`. Generated
headers belong in `generated_inputs` on the target or only on the source that
includes them.

## Running and testing

`c.run()` creates a task with no outputs, so it runs whenever selected:

```elf
run ::= c.run({ target = app, arguments = { "--version" } })
entries.run = fun() {
	ret manny.build({ targets = { run } })
}
```

`c.test()` combines an executable target and a run task:

```elf
tests ::= c.test({
	name = "math_tests",
	sources = { "tests/math_tests.c" },
	dependencies = { math },
})

entries.test = fun() {
	ret manny.build({ targets = { tests.task } })
}
```

Raw Manny tasks and helper-generated targets can freely depend on one another.
