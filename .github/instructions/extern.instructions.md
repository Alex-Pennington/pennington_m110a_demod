---
applyTo: "extern/**"
---

# External Dependencies - DO NOT MODIFY

## Purpose

This folder contains external dependencies that are managed as git submodules or external code.

## Contents

| Folder | Source | Purpose |
|--------|--------|---------|
| `brain_core/` | Git submodule | Paul's M110A modem implementation |
| `codec2/` | Git submodule | Open source voice codec |
| `brain_wrapper.h` | Local | Wrapper header for brain_core |

## Rules

1. **DO NOT MODIFY** code inside `brain_core/` or `codec2/` - these are external projects.

2. **Submodule Updates**: To update submodules, use:
   ```
   git submodule update --remote
   ```

3. **brain_wrapper.h**: This is a local wrapper and CAN be modified if needed to adapt the external API.

4. **Build Integration**: Brain Core is built via its own `build.ps1` and the output is copied to `release/bin/`.

## If You Need to Change brain_core

Changes to brain_core must be made in the separate `d:\brain_core` repository, not here. This folder is just a submodule reference.
