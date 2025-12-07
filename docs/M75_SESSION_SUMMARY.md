# M75 Walsh Decode Investigation - Session Summary

## Date: December 2024

## Objective
Understand and implement the 75bps Walsh decode algorithm from reference MS-DMT code.

## Major Achievements

### 1. Complete Algorithm Documentation
Created comprehensive documentation of the M75 Walsh decode algorithm:
- Complete TX chain: FEC → Interleave → Gray → Walsh → Scramble → 8PSK
- Complete RX chain: Correlate → Best Match → Gray Decode → Deinterleave → Viterbi
- sync_75_mask adaptive timing explained
- Frame structure: 15 Walsh/frame, MES every 45 blocks

### 2. Working Walsh75Decoder Class
Implemented `/home/claude/m110a_demod/src/m110a/walsh_75_decoder.h`:
- 4 Walsh patterns (MNS/MES)
- sync_75_mask adaptive timing
- Scrambler integration
- Gray decode to soft bits
- **Loopback test: 10/10 perfect**

### 3. Real Signal Partial Decode
Successfully decoded first 2 characters of "Hello":
- Found correct scrambler start position (45)
- Found correct symbol offset (3838)
- Walsh correlations: ~48000 magnitude (very strong)
- Output: `He` (0x48 0x65) correct, third byte wrong

## Key Discoveries

### Scrambler Initialization
The scrambler does NOT start at position 0 for data decode. Testing found:
- scrambler_start = 45 produces "He" correctly
- Other values produce garbage

### Two Strong Correlation Regions
Found two regions with strong Walsh correlations in the test file:
1. Offset 1572: Pattern `200310011103212`
2. Offset 3838: Pattern `002331223213203` (better decode)

### Bit Error Pattern
The third byte has a 2-bit error:
- Expected: 0x6c ('l')
- Got: 0x60
- Error: bits 2 and 3 flipped

## Remaining Issues

1. **Bit Errors Beyond Byte 2**
   - Likely interleaver timing or MES/MNS block tracking issue
   - Could be related to where data actually starts

2. **Scrambler State**
   - Need to verify scrambler advances correctly across Walsh symbols
   - May need to track state separately for each interleaver block

3. **MES Block Timing**
   - First block might be MES, not MNS
   - Reference code pre-increments block count

## Files Created

### Source Code
- `src/m110a/walsh_75_decoder.h` - Complete Walsh decoder class

### Test Programs
- `test/test_walsh_75_decoder.cpp` - Basic Walsh decoder test
- `test/test_m75_complete.cpp` - Full pipeline test
- `test/test_m75_full_decode.cpp` - Scrambler search test
- `test/test_m75_tx_sim.cpp` - TX simulation
- `test/test_m75_offset_scan.cpp` - Offset scanning

### Documentation
- `docs/M75_WALSH_ALGORITHM.md` - Complete algorithm documentation
- `docs/M75_SESSION_SUMMARY.md` - This file
- `docs/TODO_M75_WALSH.md` - Investigation checklist

## Next Steps

1. **Debug Bit Errors**
   - Trace through interleaver with known values
   - Verify MES/MNS block assignment
   - Check if scrambler state needs reset at block boundaries

2. **Integration**
   - Add M75 support to MultiModeRx
   - Create proper test infrastructure
   - Test M75NL (long interleave)

## Reference Code Locations
- `de110a.cpp:706` - decode_75bps_data()
- `de110a.cpp:647` - accumulate_75bps_symbol()
- `de110a.cpp:611` - reset_sync_75_mask()
- `rxm110a.cpp:748` - match_sequence()
- `t110a.cpp:148-162` - Walsh tables
- `in110a.cpp:163` - M75NS interleaver params
