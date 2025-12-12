# Documentation Alignment Plan

**Created:** December 12, 2025  
**Purpose:** Track documentation updates following the cross-modem interoperability breakthrough  
**Status:** 🔄 IN PROGRESS

---

## 🎉 Milestone Achievement

**Phoenix Nest TX → Brain Core RX: 9/12 modes PASSING!**

This is a major milestone that validates MIL-STD-188-110A compliance and interoperability with the Qt MSDMT "de facto standard" implementation.

---

## Documents to Review & Update

### Priority 1: Main Project Files (Root)

| # | File | Current State | Required Updates | Status |
|---|------|---------------|------------------|--------|
| 1 | [README.md](README.md) | Outdated | Add interop status, update test results | ❌ TODO |
| 2 | [CURRENT_WORK.md](CURRENT_WORK.md) | ✅ Updated | Already reflects breakthrough | ✅ DONE |

### Priority 2: Core Documentation (docs/)

| # | File | Current State | Required Updates | Status |
|---|------|---------------|------------------|--------|
| 3 | [docs/README.md](docs/README.md) | Generic | Add interop test results section | ❌ TODO |
| 4 | [docs/API.md](docs/API.md) | Technical | Review accuracy, minor updates | ❌ TODO |
| 5 | [docs/TX_CHAIN.md](docs/TX_CHAIN.md) | Technical | Add note about brain_preamble.h fix | ❌ TODO |
| 6 | [docs/RX_CHAIN.md](docs/RX_CHAIN.md) | Technical | Review for accuracy | ❌ TODO |
| 7 | [docs/PROTOCOL.md](docs/PROTOCOL.md) | Reference | Update D1/D2 section if needed | ❌ TODO |
| 8 | [docs/M110A_MODES_AND_TEST_MATRIX.md](docs/M110A_MODES_AND_TEST_MATRIX.md) | Has test matrix | Update with interop results | ❌ TODO |
| 9 | [docs/EQUALIZERS.md](docs/EQUALIZERS.md) | Technical | Review for accuracy | ❌ TODO |
| 10 | [docs/CHANNEL_SIMULATION.md](docs/CHANNEL_SIMULATION.md) | Technical | Review for accuracy | ❌ TODO |

### Priority 3: Reference Documentation (docs/)

| # | File | Current State | Required Updates | Status |
|---|------|---------------|------------------|--------|
| 11 | [docs/QT_MSDMT_REFERENCE.md](docs/QT_MSDMT_REFERENCE.md) | Reference | Update status table, mark items done | ❌ TODO |
| 12 | [docs/CURRENT_UNDERSTANDING_MSDMT.md](docs/CURRENT_UNDERSTANDING_MSDMT.md) | Detailed analysis | Add note about fix applied | ❌ TODO |
| 13 | [docs/TCPIP Guide.md](docs/TCPIP%20Guide.md) | Usage guide | Review server ports, commands | ❌ TODO |

### Priority 4: Server Documentation

| # | File | Current State | Required Updates | Status |
|---|------|---------------|------------------|--------|
| 14 | [server/README.md](server/README.md) | Server docs | Update with interop status | ❌ TODO |
| 15 | [api/README.md](api/README.md) | API docs | Review for accuracy | ❌ TODO |

---

## Detailed Update Plan

### 1. README.md (Root) - PRIORITY HIGH

**Current Issues:**
- No mention of cross-modem interoperability
- Test results section doesn't show interop tests
- No mention of Qt MSDMT compatibility

**Required Changes:**
- [ ] Add "Interoperability" section after Features
- [ ] Add interop test results table
- [ ] Update "Test Results" section with cross-modem results
- [ ] Mention Brain Core (Qt MSDMT) compatibility
- [ ] Update version/status if applicable

### 2. docs/M110A_MODES_AND_TEST_MATRIX.md - PRIORITY HIGH

**Current Issues:**
- Test matrix may not reflect current interop status

**Required Changes:**
- [ ] Add cross-modem interop test results section
- [ ] Update mode support table with interop status
- [ ] Note 75 bps modes not tested (TX not implemented)

### 3. docs/QT_MSDMT_REFERENCE.md - PRIORITY MEDIUM

**Current Issues:**
- Status table shows many items as ❌ NO (now fixed)

**Required Changes:**
- [ ] Update "Our Implementation Status" table
- [ ] Add section on what was fixed
- [ ] Mark brain_wrapper.h alignment as complete

### 4. docs/TX_CHAIN.md - PRIORITY MEDIUM

**Current Issues:**
- No mention of brain_preamble.h or preamble encoding details

**Required Changes:**
- [ ] Add note about preamble generation conforming to Qt MSDMT
- [ ] Reference brain_preamble.h for preamble encoding

### 5. docs/CURRENT_UNDERSTANDING_MSDMT.md - PRIORITY LOW

**Current Issues:**
- Analysis document, may need update note

**Required Changes:**
- [ ] Add "Resolution" section noting the fix
- [ ] Mark document as "implemented" not just "analysis"

### 6. Other Documents - PRIORITY LOW

**docs/README.md, docs/API.md, docs/RX_CHAIN.md, docs/EQUALIZERS.md, docs/CHANNEL_SIMULATION.md, docs/PROTOCOL.md:**
- Review for factual accuracy
- No major updates expected unless errors found

---

## Key Facts to Include

### Interoperability Status
```
Phoenix Nest TX → Brain Core RX: 9/12 modes PASS
Brain Core TX → Phoenix Nest RX: 10/12 modes PASS (from commit 34ba342)
```

### Mode Results Table
| Mode | PN→BC | BC→PN | Notes |
|------|-------|-------|-------|
| 600S | ✅ | ✅ | Full interop |
| 600L | ✅ | ✅ | Full interop |
| 300S | ✅ | ✅ | Full interop |
| 300L | ✅ | ✅ | Full interop |
| 1200S | ✅ | ✅ | Full interop |
| 1200L | ✅ | ✅ | Full interop |
| 2400S | ✅ | ✅ | Full interop |
| 2400L | ✅ | ✅ | Full interop |
| 150S | ⚠️ | ✅ | PN→BC: 4/5 bytes decoded |
| 150L | ✅ | ✅ | Full interop |
| 75S | ❌ | ❌ | TX not implemented |
| 75L | ❌ | ❌ | TX not implemented |

### Root Cause Summary
**Problem:** `src/m110a/brain_preamble.h::encode_frame()` had 3 bugs:
1. Scrambler index was continuous across frame (should restart per segment)
2. Count segment encoding was wrong (used countdown%8, should use add_count_seq pattern)
3. Zero segment was outputting raw pscramble (should use psymbol[0] + scramble)

**Fixed:** December 12, 2025 - Commit d73e5e5

---

## Progress Tracking

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Create this tracking document | ✅ DONE |
| 2 | Update README.md (root) | ❌ TODO |
| 3 | Update docs/M110A_MODES_AND_TEST_MATRIX.md | ❌ TODO |
| 4 | Update docs/QT_MSDMT_REFERENCE.md | ❌ TODO |
| 5 | Update docs/TX_CHAIN.md | ❌ TODO |
| 6 | Review remaining docs for accuracy | ❌ TODO |
| 7 | Final review with user | ❌ TODO |
| 8 | Delete this file and commit | ❌ TODO |

---

## Change Log

| Date | Change | By |
|------|--------|-----|
| 2025-12-12 | Document created | Copilot |

---

*This document will be deleted after all updates are confirmed and committed.*
