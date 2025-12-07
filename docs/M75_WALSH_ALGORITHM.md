# M75 Walsh Decode Algorithm - Complete Documentation

## Overview
The M75 (75 bps) mode uses Walsh-Hadamard coding with 32-symbol sequences
to achieve extremely robust communication at very low data rates.

## Algorithm Summary

### Data Flow (TX)
```
Input bits → FEC (K=7, R=1/2) → Interleaver → Gray Code → Walsh Encode → Scramble → 8PSK → RF
```

### Data Flow (RX)
```
RF → Baseband → Walsh Correlate (×4) → Best Match → Gray Decode → Deinterleaver → Viterbi → Output
```

## Complete TX Chain

### 1. FEC Encoding
- K=7 constraint length convolutional code
- Rate 1/2 (G1=0133, G2=0171 octal)
- 5 bytes input → 80 bits → 160 coded bits

### 2. Interleaving
**M75NS (Short)**:
- Matrix: 10 rows × 9 cols = 90 bits
- Write: row_inc=7, col_inc=2
- MES every 45 Walsh symbols

**M75NL (Long)**:
- Matrix: 20 rows × 36 cols = 720 bits
- Write: row_inc=7, col_inc=29
- MES every 360 Walsh symbols

### 3. Gray Encoding (mgd2)
```
Data bits → Walsh index:
  00 → 0
  01 → 1
  10 → 3 (swapped with 2!)
  11 → 2
```

### 4. Walsh Encoding
Each 2-bit dibit selects one of 4 Walsh patterns (32 symbols each):
```
MNS (Mode Normal Status):
  Pattern 0: 00000000... (all +1)
  Pattern 1: 04040404... (+1,-1,+1,-1...)
  Pattern 2: 00440044... (+1,+1,-1,-1...)
  Pattern 3: 04400440... (+1,-1,-1,+1...)

MES (Mode/Error Status) - every block_count_mod blocks:
  Pattern 0: 00004444... (4× +1, 4× -1, repeat)
  Pattern 1: 04044040...
  Pattern 2: 00444400...
  Pattern 3: 04400440...
```
Note: Values 0 and 4 represent 8PSK positions 0° and 180° (BPSK)

### 5. Scrambling
```
TX: output_symbol = (walsh_symbol + scrambler_tribit) % 8
```
- 12-bit LFSR scrambler
- Polynomial: x^12 + x^7 + x^5 + x^2 + 1
- Init: 101101011101
- Clocked 8× per output tribit
- Period: 160 tribits

## Complete RX Chain

### 1. Symbol Extraction
- Reference operates at 9600 Hz, decimates to 4800 Hz with matched filter
- Our MSDMT outputs at 2400 Hz (need to duplicate for compatibility)
- Each Walsh symbol = 32 8PSK symbols at 2400 Hz = 64 samples at 4800 Hz

### 2. Walsh Correlation (decode_75bps_data)
```c
For each of 4 Walsh patterns:
    1. scramble_75bps_sequence(walsh_pattern, expected, scrambler_count)
       - Applies scrambler rotation to expected pattern
    
    2. mag = accumulate_75bps_symbol(input, correlation_array, expected)
       - Performs 32 sliding correlations at offsets 0-31
       - Each uses match_sequence() with i*2 indexing
       - Weights results by sync_75_mask[]
       - Returns weighted sum
    
    3. Track best match (highest magnitude)

soft_decision = sqrt(best_mag / total_mag)
```

### 3. match_sequence() - Core Correlation
```c
float match_sequence(FComplex *in, FComplex *seq, int length) {
    FComplex temp = {0, 0};
    for (int i = 0; i < length; i++) {
        temp.re += in[i*2].re * seq[i].re + in[i*2].im * seq[i].im;  // Real part of conj multiply
        temp.im += in[i*2].im * seq[i].re - in[i*2].re * seq[i].im;  // Imag part of conj multiply
    }
    return temp.re*temp.re + temp.im*temp.im;  // Magnitude squared
}
```
Note: i*2 spacing because input is at 4800 Hz (2 samples per symbol)

### 4. sync_75_mask - Adaptive Timing
```
Purpose: 32-element weighting array for timing/channel adaptation

Initialize: From preamble correlation magnitudes (m_p_mag)

Update (IIR filter after each Walsh decode):
    sync_75_mask[i] = sync_75_mask[i] * 0.50 + new_correlation[i] * 0.01

Usage in accumulate_75bps_symbol():
    output = Σ (match_sequence(&in[i], expected, 32) * sync_75_mask[i])
    
This creates a weighted sum of correlations at 32 different timing offsets,
adapting to multipath and timing drift.
```

### 5. Gray Decode to Deinterleaver
```
Walsh detected → Soft bits loaded to deinterleaver:
  0 → (+soft, +soft) = 00
  1 → (+soft, -soft) = 01
  2 → (-soft, -soft) = 11 (note: NOT 10!)
  3 → (-soft, +soft) = 10 (note: NOT 11!)

This inverts the TX Gray code exactly.
```

### 6. Frame Timing
- 200ms per frame = 480 symbols at 2400 Hz
- 15 Walsh symbols per frame (15 × 32 = 480)
- 15 Walsh × 2 bits = 30 coded bits per frame
- 30 bits × 5 frames/sec = 150 coded bits/sec
- With rate 1/2 FEC = 75 data bits/sec

### 7. MES/MNS Scheduling
```
rx_block_count increments by 1 per Walsh symbol
When rx_block_count == rx_block_count_mod:
    Use MES sequences (every 45th for M75NS, every 360th for M75NL)
    Reset rx_block_count = 0
Otherwise:
    Use MNS sequences
```

### 8. Scrambler Advancement
```
rx_scrambler_count += 32 per Walsh symbol (modulo 160)
symbol_offset += 64 per Walsh symbol (64 samples at 4800 Hz)
```

## Key Constants

| Parameter | M75NS | M75NL |
|-----------|-------|-------|
| Interleaver rows | 10 | 20 |
| Interleaver cols | 9 | 36 |
| Interleaver size | 90 bits | 720 bits |
| row_inc | 7 | 7 |
| col_inc | 2 | 29 |
| block_count_mod | 45 | 360 |
| D1 | 7 | 5 |
| D2 | 5 | 5 |
| Preamble frames | 3 | 24 |

## Detailed Algorithm Pseudocode

### accumulate_75bps_symbol()
```
function accumulate_75bps_symbol(input_symbols, output_array, expected_pattern):
    total_output = 0
    
    for offset = 0 to 31:
        # Correlate 32 symbols starting at this offset
        output_array[offset] = match_sequence(&input[offset], expected_pattern, 32)
        
        # Weight by adaptive mask
        total_output += output_array[offset] * sync_75_mask[offset]
    
    return total_output
```

### decode_75bps_data()
```
function decode_75bps_data(input_symbols, scrambler_count, is_mes):
    # Select which Walsh pattern set to use
    patterns = mes_seq if is_mes else mns_seq
    
    # Correlate against all 4 patterns
    best_data = 0
    best_magnitude = 0
    total_magnitude = 0
    correlation_arrays[4][32]
    
    for pattern_idx = 0 to 3:
        # Apply scrambler to expected pattern
        scrambled_pattern = scramble_75bps_sequence(patterns[pattern_idx], scrambler_count)
        
        # Correlate with adaptive timing
        magnitude = accumulate_75bps_symbol(input, correlation_arrays[pattern_idx], scrambled_pattern)
        
        total_magnitude += magnitude
        if magnitude > best_magnitude:
            best_magnitude = magnitude
            best_data = pattern_idx
    
    # Update adaptive timing mask with winning pattern's correlations
    update_sync_75_mask(correlation_arrays[best_data])
    
    # Calculate soft decision
    soft = sqrt(best_magnitude / total_magnitude)
    
    # Gray decode and load to deinterleaver
    switch best_data:
        case 0: load_deinterleaver(+soft, +soft)  # bits 00
        case 1: load_deinterleaver(+soft, -soft)  # bits 01
        case 2: load_deinterleaver(-soft, -soft)  # bits 11
        case 3: load_deinterleaver(-soft, +soft)  # bits 10
```

### Frame Processing Loop
```
function process_75bps_frame(input_4800hz):
    symbol_offset = 0
    
    for walsh_idx = 0 to 14:  # 15 Walsh symbols per 200ms frame
        # Check if this is an MES block
        rx_block_count++
        is_mes = (rx_block_count == block_count_mod)
        if is_mes:
            rx_block_count = 0
        
        # Decode Walsh symbol
        decode_75bps_data(&input[symbol_offset], rx_scrambler_count, is_mes)
        
        # Advance positions
        rx_scrambler_count = (rx_scrambler_count + 32) % 160
        symbol_offset += 64  # 64 samples at 4800 Hz = 32 symbols
```

## Implementation Notes

### Timing Considerations
1. The sync_75_mask provides timing tolerance of about ±16 symbol periods
2. Initial mask values come from preamble correlation (good initial timing estimate)
3. The IIR update tracks slow timing drift

### Why sync_75_mask Works
- In a multipath/fading channel, signal energy spreads across multiple timing offsets
- The mask learns which offsets have signal energy
- Better offsets get higher weights, improving effective SNR
- The 0.50/0.01 update rates balance tracking vs noise smoothing

### Sample Rate Conversion
Our implementation extracts symbols at 2400 Hz (one sample per symbol).
The reference expects 4800 Hz (two samples per symbol).

Solutions:
1. **Duplicate symbols**: Each 2400 Hz symbol appears twice at 4800 Hz
2. **Modify correlation**: Skip i*2 indexing, use consecutive samples
3. **Resample MSDMT output**: Add interpolation to 4800 Hz

Option 1 (duplication) is simplest and should work for clean signals.

## Test Files
- tx_75S_20251206_202410_888.pcm - M75NS test file (5 bytes "Hello")
- tx_75L_20251206_202421_539.pcm - M75NL test file

## Reference Code Files
- de110a.cpp: decode_75bps_data(), accumulate_75bps_symbol(), sync_75_mask
- rxm110a.cpp: match_sequence()
- txm110a.cpp: TX encoding
- t110a.cpp: Walsh tables (mns, mes), scrambler, Gray code (mgd2)
- in110a.cpp: Interleaver parameters

## Implementation Status (December 2024)

## Implementation Status (December 2024)

### Working Components
1. **Walsh75Decoder class** - `/home/claude/m110a_demod/src/m110a/walsh_75_decoder.h`
   - Loopback test: 10/10 perfect
   - Real file: Strong correlations (~3500 magnitude) after sync_mask warmup
   - Gray decode to soft bits working
   - sync_75_mask adaptive timing working

2. **MSDMT Symbol Extraction** - Extracts 2400 Hz symbols correctly
   - Mode detection: D1=7, D2=5 (M75NS) confirmed
   - Symbol duplication to 4800 Hz for correlation

### Major Discoveries
1. **Scrambler Initialization**: Must start at 45 (not 0) for this test file
2. **Best Symbol Offset**: 3838 (in 4800 Hz duplicated stream)
3. **Walsh Correlation Strength**: ~48000 magnitude at correct timing
4. **Partial Decode Success**: Getting "He" (first 2 chars) correctly
   - Byte 0: 0x48 'H' ✓
   - Byte 1: 0x65 'e' ✓
   - Byte 2: 0x60 instead of 0x6c 'l' ✗

### Remaining Issues
1. **Bit Errors After First 2 Bytes**
   - 0x60 vs 0x6c = 2-bit error (bits 2,3)
   - Could be interleaver timing, MES/MNS issue, or scrambler state

### Test Results
```
Walsh75Decoder Loopback: 10/10 correct ✓
Real file (tx_75S_20251206_202410_888.pcm):
  - Best offset: 3838 with scrambler=45
  - Walsh correlations: ~48000 magnitude
  - Decoded: "He`" (2/5 chars correct)
```

### Key Files
- `/home/claude/m110a_demod/src/m110a/walsh_75_decoder.h` - Walsh decoder class
- `/home/claude/m110a_demod/test/test_m75_complete.cpp` - Main test program  
- `/home/claude/m110a_demod/test/test_m75_full_decode.cpp` - Scrambler search test
- `/home/claude/m110a_demod/docs/M75_WALSH_ALGORITHM.md` - Algorithm documentation
