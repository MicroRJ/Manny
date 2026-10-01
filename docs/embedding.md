---
title: Embedding Manny
label: Reference
description: Using Manny's generic graph executor as a C library.
---

The abstract heart of Manny is independent of command lines, files, and elf.

A `Manny` contains nodes and dependency edges. Each node has a callback, user data, and a result. `manny_execute()` runs the reachable graph using the requested worker count and can report start and completion events to the host.

The build layer sits on top of that executor and adds:

- process execution;
- file inputs and outputs;
- task fingerprints;
- compiler-generated dependencies;
- persistent incremental state.

The elf frontend sits above the build layer and turns script tables into build tasks. It is one way to drive Manny, not a dependency of the generic graph executor.

The public C declarations currently live in [`src/graph/manny.h`](https://github.com/MicroRJ/Manny/blob/master/src/graph/manny.h). The API is still allowed to evolve while Manny remains in early development.
