# PhoenixNest RX Decode Status Report

**Date:** December 10, 2025 (Updated)  
**Reporter:** Alex Pennington  
**Build:** 1.2.0 (turbo branch, build 193, commit 38602b4)  
**Platform:** Windows, MinGW/g++

---

## Executive Summary

After applying two critical fixes:
1. **D1/D2 position fix** in `msdmt_decoder.h` (symbol positions 320/352, not 288/320)
2. **Preamble length fix** - use configured `preamble_symbols` instead of deriving from detected mode name

**Result: 6/12 modes now passing (was 0/12)**

---

## Current Test Results

| Mode | DCD | Mode Detected | Bytes Decoded | Content | Result |
|------|-----|---------------|---------------|---------|--------|
| 75S  | ❌ NO | - | 0 | - | FAIL (not implemented) |
| 75L  | ❌ NO | - | 0 | - | FAIL (not implemented) |
| 150S | ✓ YES | 150 BPS SHORT | 65 | Garbage | FAIL |
| 150L | ✓ YES | 150 BPS LONG | 90 | Garbage | FAIL |
| 300S | ✓ YES | 300 BPS SHORT | 103 | ✓ "THE QUICK BROWN FOX..." | **PASS** |
| 300L | ✓ YES | 300 BPS LONG | 57 | ✓ "THE QUICK BROWN FOX..." | **PASS** |
| 600S | ✓ YES | 600 BPS SHORT | 133 | ✓ "THE QUICK BROWN FOX..." | **PASS** |
| 600L | ✓ YES | 600 BPS LONG | 360 | Partial: " FOX JUMPS OVER..." | FAIL |
| 1200S | ✓ YES | 1200 BPS SHORT | 57 | ✓ "THE QUICK BROWN FOX..." | **PASS** |
| 1200L | ✓ YES | 1200 BPS LONG | 720 | ✓ Contains expected message | **PASS** |
| 2400S | ✓ YES | 2400 BPS SHORT | 174 | Garbage | FAIL |
| 2400L | ✓ YES | 2400 BPS LONG | 1437 | ✓ Contains expected message | **PASS** |

---

## Fixes Applied

### Fix 1: D1/D2 Position Correction (msdmt_decoder.h)

**File:** `src/m110a/msdmt_decoder.h`

```cpp
// BEFORE (lines 529-531):
// D1 starts at symbol 288, D2 at 320
int d1_start = result.start_sample + 288 * sps_;
int d2_start = result.start_sample + 320 * sps_;

// AFTER:
// D1 starts at symbol 320, D2 at 352 (per MIL-STD-188-110A)
int d1_start = result.start_sample + 320 * sps_;
int d2_start = result.start_sample + 352 * sps_;

// Also fixed PSCRAMBLE indices (lines 542, 557):
// BEFORE: msdmt::pscramble[(288 + i) % 32]
// AFTER:  msdmt::pscramble[i % 32]
```

### Fix 2: Preamble Length for LONG Modes (msdmt_decoder.h + modem_rx.cpp)

**Root Cause:** `extract_data_symbols()` was using detected mode name to determine preamble length, but D1/D2 detection could return wrong mode, causing data extraction to start at wrong position.

**File:** `src/m110a/msdmt_decoder.h`
```cpp
// BEFORE:
int preamble_symbols = 1440;  // Default to short
if (result.mode_name.back() == 'L') {
    preamble_symbols = 11520;
}

// AFTER:
// Use configured preamble length (set by caller based on known mode)
int preamble_symbols = config_.preamble_symbols;
```

**File:** `api/modem_rx.cpp`
```cpp
// Added line ~133:
decode_cfg.preamble_symbols = mode_cfg.preamble_symbols();  // Critical for LONG modes!
```

---

## Remaining Issues

### Issue 1: 150S/150L - Garbage Output
- **Symptom:** DCD acquired, mode detected, but output is garbage
- **Likely Cause:** 150 BPS uses 4× symbol repetition (each FEC bit repeated 4 times). The repetition combining in `decode_soft_bits()` may not be handling this correctly.
- **Mode Config:** `symbol_repetition = 4`, `unknown_data_len = 20`, `known_data_len = 20`

### Issue 2: 600L - Partial Decode
- **Symptom:** Decodes " FOX JUMPS OVER THE LAZY DOG..." (missing "THE QUICK BROWN")
- **Likely Cause:** Small timing/phase issue at beginning of data, or first interleaver block being corrupted
- **Note:** 600S works perfectly, so SHORT interleave logic is correct

### Issue 3: 2400S - Garbage Output
- **Symptom:** DCD acquired, mode detected, but output is garbage
- **Likely Cause:** 2400 BPS uses 8-PSK modulation. Different demapper or timing sensitivity.
- **Mode Config:** `Modulation::PSK8`, `symbol_repetition = 1`

### Issue 4: 75S/75L - Not Implemented
- **Symptom:** No DCD
- **Cause:** Explicitly marked as not implemented in code

---

## Files Modified

1. `src/m110a/msdmt_decoder.h` - D1/D2 position fix + preamble_symbols config usage
2. `api/modem_rx.cpp` - Set `decode_cfg.preamble_symbols` from mode config

---

## Next Steps

1. **Debug 150 BPS repetition combining** - Verify `decode_soft_bits()` pattern for rep=4
2. **Investigate 600L partial decode** - Check first interleaver block boundary
3. **Debug 2400S 8-PSK path** - May need different phase tracking or timing for 8-PSK

---

## Build Information

```
Build: 193
Branch: turbo
Commit: 38602b4
Date: 2025-12-10
```
