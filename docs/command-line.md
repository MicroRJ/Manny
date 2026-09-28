---
title: Command line
label: Reference
description: Manny command-line usage and options.
---

```text
manny [build-file] [entry] [options]
```

Manny uses `build.elf` and the `build` entry when neither is specified:

```text
manny
manny build
manny run
manny clean
```

An entry is an elf function exposed by the build script. It can construct and execute a graph, generate files, remove build output, or perform any other scripted operation.

## Options

```text
-q, --quiet          Suppress task output.
--explain            Explain incremental decisions.
--verbose [N]        Control Manny's internal diagnostic verbosity.
--workers N          Set the worker count.
--profile            Write profiling information.
--profile-threads    Include worker-thread profiling.
--version            Print Manny and elf versions.
```

Verbosity controls Manny's own diagnostics. Task output is printed by default and is controlled separately by `--quiet`.
