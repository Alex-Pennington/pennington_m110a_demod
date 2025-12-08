# Code Review Summary - M110A VS Code Version
## 2025-12-08

### Overall Assessment: GOOD with ONE CRITICAL BUG

Your codebase is solid. The server infrastructure, test framework, and PowerShell
build system are nice additions. DFE as default is fine - your tests show it works.

---

## 🚨 CRITICAL BUG: D1/D2 Mode Detection Positions

**Impact:** External interoperability BROKEN (decoding signals from real radios, MS-DMT software)

**Why Loopback Tests Still Pass:** The bug is symmetric - TX and RX both use wrong positions,
so they cancel out. Your modem talks to itself fine, but won't decode external signals.

**Files Affected:**
1. `src/m110a/mode_detector.h` - MAIN FIX NEEDED
2. `src/m110a/msdmt_decoder.h` - Secondary fix

**Current (WRONG):**
```
D1: symbols 288-383 (96 symbols)
D2: symbols 480-575 (96 symbols)
```

**Correct:**
```
D1: symbols 320-351 (32 symbols)
D2: symbols 352-383 (32 symbols)
```

**Fix Files Provided:**
- `mode_detector_FIXED.h` - Drop-in replacement for src/m110a/mode_detector.h
- `CRITICAL_PATCH_D1D2_FIX.md` - Full explanation with validation results
- `PATCH_msdmt_decoder_D1D2.txt` - Line-by-line changes for msdmt_decoder.h

---

## What You're Keeping (Confirmed Good) ✅

| Component | Status | Notes |
|-----------|--------|-------|
| Server infrastructure | ✅ | Unique value - TCP/IP MS-DMT compatible |
| PowerShell build | ✅ | Clean, version management works |
| Test framework | ✅ | 3.8x speedup, good coverage |
| DFE as default | ✅ | Your tests show 98.6% pass rate |
| Channel simulation | ✅ | Extensive api/channel_sim.h |

---

## What NOT to Merge (Agreed) 

| Component | Reason |
|-----------|--------|
| CMake build | PowerShell works fine |
| Header-only architecture | Your compiled approach works |
| Major modem core changes | Risks breaking working code |

---

## Minor Observations (Not Bugs)

1. **SISO Polynomials** - Your siso_viterbi.h has poly_g1=0133, poly_g2=0171.
   This matches your viterbi.h. Verified correct.

2. **MIL-STD Citations** - Consider adding spec references to tables for legal clarity:
   - PSYMBOL → "MIL-STD-188-110A Table C-VII"
   - PSCRAMBLE → "MIL-STD-188-110A Section C.5.2.1"

---

## Validation After Fix

Test against reference samples in `refrence_pcm/`:
- tx_150S, tx_150L, tx_300S, tx_300L, tx_600S, tx_600L
- tx_1200S, tx_1200L, tx_2400S, tx_2400L

All 10 should show correct D1/D2 detection after applying the fix.

---

## Questions?

The fix is straightforward - just wrong symbol positions. Your architecture is fine,
your tests are good, this was just a subtle interoperability bug that loopback
testing can't catch.
