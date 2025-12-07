# TODO: M75 Walsh Decode Algorithm Investigation

## Overview
The M75 (75 bps) mode uses Walsh-Hadamard coding with 32-symbol sequences
to achieve extremely robust communication at very low data rates.

## Investigation Tasks

### Phase 1: TX Chain Analysis ✓ PARTIALLY COMPLETE
- [x] Identify Walsh sequences (MNS/MES) - t110a.cpp lines 148-162
- [x] Understand mgd2 Gray mapping - {0,1,3,2}
- [x] Find scrambler application - transmit_unknown_data_symbol() adds scrambler
- [ ] Document complete TX flow from bits to symbols
- [ ] Understand MES vs MNS selection (tx_block_count_mod = 45)

### Phase 2: RX Chain Analysis - IN PROGRESS
- [x] Find decode_75bps_data() - de110a.cpp line 706
- [x] Find scramble_75bps_sequence() - de110a.cpp line 685
- [x] Find accumulate_75bps_symbol() - de110a.cpp line 647
- [x] Find match_sequence() - rxm110a.cpp line 748 (uses i*2 spacing)
- [ ] Document sync_75_mask adaptive weighting
- [ ] Understand rx_scrambler_count advancement
- [ ] Document soft decision calculation
- [ ] Understand Gray decode to deinterleaver

### Phase 3: Frame Structure
- [ ] Document 200ms frame layout for 75bps
- [ ] Verify 15 Walsh symbols per frame (15 × 32 = 480 symbols @ 2400 Hz)
- [ ] Understand MES every 45 blocks
- [ ] Document symbol_offset += 64 per Walsh decode

### Phase 4: Deinterleaver Integration
- [ ] Verify M75NS params: rows=10, cols=9, row_inc=7, col_inc=2, mod=45
- [ ] Verify M75NL params: rows=20, cols=36, row_inc=7, col_inc=29, mod=360
- [ ] Understand bits per interleaver load

### Phase 5: Implementation
- [ ] Create Walsh75Decoder class
- [ ] Integrate with MSDMT symbol extraction
- [ ] Add sync_75_mask support
- [ ] Test with tx_75S file
- [ ] Test with tx_75L file

## Key Findings So Far

### Walsh Sequences (BPSK at 0° and 180°)
```
MNS (Mode Normal Status) - most data blocks:
  0: all zeros  → BPSK +1 (32 chips)
  1: 0,4 alternating → +1,-1 pattern
  2: 0,0,4,4 pattern → +1,+1,-1,-1
  3: 0,4,4,0 pattern → +1,-1,-1,+1

MES (Mode/Error Status) - every 45th block:
  0: 0,0,0,0,4,4,4,4 repeated
  1: 0,4,0,4,4,0,4,0 repeated
  2: 0,0,4,4,4,4,0,0 repeated  
  3: 0,4,4,0,4,0,0,4 repeated
```

### Scrambler
- 12-bit LFSR, polynomial x^12 + x^7 + x^5 + x^2 + 1
- Init: 101101011101
- Clocked 8x per output tribit
- Period: 160 tribits
- Applied as: (walsh_val + scrambler_tribit) % 8

### Sample Rates
- Reference modem: 9600 Hz input → matched filter → 4800 Hz symbols
- Our MSDMT: 48000 Hz input → RRC filter → 2400 Hz symbols
- match_sequence() uses in[i*2] indexing on 4800 Hz data

### Gray Code (mgd2)
TX: data_bits → mgd2 → Walsh index
  00 → 0 → MNS/MES[0]
  01 → 1 → MNS/MES[1]  
  10 → 3 → MNS/MES[3]
  11 → 2 → MNS/MES[2]

RX: Walsh_detected → soft_bits (inverted Gray)
  0 → (+soft, +soft)
  1 → (+soft, -soft)
  2 → (-soft, -soft)
  3 → (-soft, +soft)

### Mode IDs
- M75NS: D1=7, D2=5 (short interleave)
- M75NL: D1=5, D2=5 (long interleave)

## Questions to Answer
1. Why does sync_75_mask improve decode?
2. How is timing acquisition done for 75bps?
3. How does frequency error affect Walsh correlation?
4. What is SYNC_75_MASK_LENGTH?

## Files to Study
- de110a.cpp: decode_75bps_data, accumulate_75bps_symbol, sync_75_mask
- rxm110a.cpp: match_sequence, frame processing
- txm110a.cpp: TX encoding
- t110a.cpp: Walsh tables, scrambler
- Cm110s.h: Constants, state variables
