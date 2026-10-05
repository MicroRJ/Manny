# Guide {#step-by-step-guide}

This guide starts with an ordinary elf script and builds up to an incremental
task graph one piece at a time.

## Get Manny {#getting-manny}

Manny currently supports Windows x64.

Download and extract the latest [Windows
preview](https://github.com/MicroRJ/Manny/releases/tag/v0.3.0-dev). Add the
directory containing `manny.exe` to your `PATH`, or invoke it using its full
path.

Check that it runs:

```bat
manny --version
```

To build Manny from source instead, clone the repository with its submodules and
run `build.bat`:

```bat
git clone --recursive https://github.com/MicroRJ/Manny.git
cd Manny
build.bat
```

The new executable is written to `build\manny.exe`.

## Run build.elf {#running-buildelf}

Create an empty directory and add a file named `build.elf`:

```elf
elf.printl("Hello, World!")

ret {
	build = fun() {},
}
```

Now run Manny from that directory:

```bat
manny
```

Manny looks for `build.elf`, loads it, and invokes the `build` entry by default.
The first line runs while the script is loaded. It is ordinary elf code, not a
special build-system declaration.

## Add entries {#entries}

The table returned by `build.elf` defines the entries you can invoke. Replace the
script with this:

```elf
ret {
	build = fun() {
		elf.printl("Building")
	},

	hello = fun() {
		elf.printl("Hello")
	},
}
```

Running Manny without an entry still invokes `build`:

```bat
manny
```

Pass another entry by name to invoke it instead:

```bat
manny hello
```

Entries are normal elf functions. They can inspect data, call other functions,
generate files, or do whatever else your project needs.

## Build a task {#tasks}

Now replace the script with one that gives Manny an actual task:

```elf
elf.fs.create_directories("build")

write_message := {
	name = "write message",
	command_line = "cmd /d /c echo Hello from Manny>build/message.txt",
	inputs = {},
	outputs = { "build/message.txt" },
	dependencies = {},
}

ret {
	build = fun() {
		ret manny.build({ targets = { write_message } })
	},
}
```

Run it:

```bat
manny
```

The entry creates no special target object. `write_message` is a plain table,
and `manny.build()` receives it as the target to build.

## Add a dependency {#dependencies}

Tasks depend directly on other task tables:

```elf
elf.fs.create_directories("build")

write_source := {
	name = "write source message",
	command_line = "cmd /d /c echo Hello from Manny>build/source.txt",
	inputs = {},
	outputs = { "build/source.txt" },
	dependencies = {},
}

copy_message := {
	name = "copy message",
	command_line = "cmd /d /c type build\\source.txt>build\\message.txt",
	inputs = { "build/source.txt" },
	outputs = { "build/message.txt" },
	dependencies = { write_source },
}

ret {
	build = fun() {
		ret manny.build({ targets = { copy_message } })
	},
}
```

Manny starts from `copy_message`, follows the reference to `write_source`, and
constructs the graph. Dependencies run before the tasks that need them.

## See incremental builds {#incremental-builds}

Run the build again without changing anything:

```bat
manny
```

Both tasks remain current because their commands, inputs, outputs, and
dependencies have not changed.

Change the message in `write_source.command_line`, then ask Manny to explain the
next build:

```bat
manny --explain
```

Manny rebuilds `write_source` because its command changed, then rebuilds
`copy_message` because its input changed.

That is the complete model: elf generates task tables, dependencies connect
them, and `manny.build()` builds the selected graph.
