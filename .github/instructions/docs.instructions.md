---
applyTo: "docs/**"
---

# Documentation Instructions

## Overview

This folder contains project documentation for the M110A modem.

## Documentation Files

| File | Purpose |
|------|---------|
| API.md | Public API reference |
| CHANNEL_SIMULATION.md | HF channel simulation |
| EQUALIZERS.md | Equalizer algorithms |
| M110A_MODES_AND_TEST_MATRIX.md | Mode specifications and test results |
| PROTOCOL.md | TCP command protocol |
| RX_CHAIN.md | Receive signal processing chain |
| TX_CHAIN.md | Transmit signal processing chain |
| TCPIP Guide.md | TCP/IP communication guide |

## Rules

1. **Keep documentation current** - Update docs when code changes
2. **Reference MIL-STD-188-110A** - Cite section numbers where applicable
3. **Include diagrams** - Signal flow diagrams help understanding
4. **Cross-reference** - Link between related documents

## Markdown Standards

- Use proper heading hierarchy (# → ## → ###)
- Use tables for structured data
- Use code blocks with language tags for examples
- Keep line length reasonable for readability

## Do NOT

- Remove existing documentation without permission
- Create duplicate documentation
- Add implementation details that belong in code comments
