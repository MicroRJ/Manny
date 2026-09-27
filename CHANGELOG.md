# Changelog

## 2026-09-27

- Renamed Bob to Manny. The executable, C API, elf library, build state, packaging, examples, and project files now use the Manny name.

## 2026-08-13

- Added BLAKE3 task fingerprints. Manny now rebuilds output tasks when their command, working directory, normalized paths, or relevant flags change, even when file timestamps have not changed.

- Replaced the textual `.manny/state.elf` dependency cache with a checksummed binary `.manny/state` snapshot. Paths are interned and tasks store compact path IDs, substantially reducing state memory, file size, and save/load time.

## 2026-08-11

- Added per-task working directories. Relative inputs and outputs are resolved from the task directory, and commands run there without changing Manny's process directory.

- Added `--explain`, which reports why each task rebuilds or remains up to date.

- Split graph execution from build behavior. Graph nodes can now run arbitrary callbacks and publish results; command execution, incremental checks, and compiler dependencies live in the build layer.

- Replaced lexical include scanning with compiler-generated dependency files. Manny now stores dependencies per task in `.manny/state.elf` and uses them for incremental rebuild checks.

## 2026-07-27

- Added a new output policy. Manny now prints program output by default. Verbosity does not control task output; it controls Manny's internal output. To suppress task output, use `--quiet` or `-q`.
