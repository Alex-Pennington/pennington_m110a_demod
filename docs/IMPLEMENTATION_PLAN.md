# MIL-STD-188-110A Complete Encode/Decode Implementation Plan

## Executive Summary

After successfully decoding M2400S (54/54 characters), this document outlines the plan to implement proper encode/decode chains for all MIL-STD-188-110A modes. The key breakthrough was discovering the scrambler must **wrap at 160 symbols**, not run continuously.

## Current Status

### Working Components ✅
- **Viterbi Encoder/Decoder**: G1=0x5B, G2=0x79, K=7 - VERIFIED CORRECT
- **MultiModeInterleaver**: Uses row_inc/col_inc properly - VERIFIED CORRECT  
- **MultiModeMapper**: Absolute PSK constellation mapping - VERIFIED CORRECT
- **Mode Configuration**: All 16 modes defined with correct parameters
- **M2400S Decoder**: 54/54 perfect decode achieved

### Components Needing Fixes 🔧
1. **Scrambler**: Must wrap at 160 symbols (critical fix identified)
2. **Bit Ordering**: LSB-first for message data in RX byte assembly
3. **Gray Code**: Must use modified Gray code tables for symbol mapping
4. **Soft Decision**: Viterbi soft bit polarity (bit 0 → +127, bit 1 → -127)
5. **Frame Structure**: Different unknown/known ratios per mode

### Reference PCM Files Available for Testing
| Mode | File | Size | Duration |
|------|------|------|----------|
| M75S | tx_75S_20251206_202410_888.pcm | 921KB | ~9.6s |
| M75L | tx_75L_20251206_202421_539.pcm | 1.7MB | ~18s |
| M150S | tx_150S_20251206_202440_580.pcm | 518KB | ~5.4s |
| M150L | tx_150L_20251206_202446_986.pcm | 1.3MB | ~13.8s |
| M300S | tx_300S_20251206_202501_840.pcm | 307KB | ~3.2s |
| M300L | tx_300L_20251206_202506_058.pcm | 1.1MB | ~11.6s |
| M600S | tx_600S_20251206_202518_709.pcm | 211KB | ~2.2s |
| M600L | tx_600L_20251206_202521_953.pcm | 1MB | ~10.6s |
| M1200S | tx_1200S_20251206_202533_636.pcm | 153KB | ~1.6s |
| M1200L | tx_1200L_20251206_202536_295.pcm | 960KB | ~10s |
| M2400S | tx_2400S_20251206_202547_345.pcm | 134KB | ~1.4s |
| M2400L | tx_2400L_20251206_202549_783.pcm | 940KB | ~9.8s |

---

## Mode-Specific Parameters

### Modulation Types
| Mode Range | Modulation | Bits/Symbol | Symbol Indices Used |
|------------|------------|-------------|---------------------|
| 75-600 bps | BPSK | 1 | 0, 4 (0°, 180°) |
| 1200 bps | QPSK | 2 | 0, 2, 4, 6 (0°, 90°, 180°, 270°) |
| 2400-4800 bps | 8PSK | 3 | 0-7 (all 8 phases) |

### Gray Code Tables (from reference modem)
```cpp
// Modified Gray codes for PSK mapping
const int MGD2[4] = {0, 1, 3, 2};        // QPSK (1200 bps)
const int MGD3[8] = {0, 1, 3, 2, 7, 6, 4, 5};  // 8PSK (2400 bps)

// Inverse tables for demapping
const int INV_MGD2[4] = {0, 1, 3, 2};    // Self-inverse for QPSK
const int INV_MGD3[8] = {0, 1, 3, 2, 6, 7, 5, 4};  // Inverse for 8PSK
```

### Frame Structure (Unknown/Known Data Pattern)
| Mode | Unknown (Data) | Known (Probe) | Pattern Repeat |
|------|----------------|---------------|----------------|
| M75NS/L | 0 | 0 | No probes - all data with Walsh coding |
| M150-M600 | 20 | 20 | 40 symbols/mini-frame |
| M1200 | 20 | 20 | 40 symbols/mini-frame |
| M2400 | 32 | 16 | 48 symbols/mini-frame |
| M4800 | 32 | 16 | 48 symbols/mini-frame |

### Interleaver Parameters
| Mode | Rows | Cols | Row_Inc | Col_Inc | Block Size |
|------|------|------|---------|---------|------------|
| M75NS | 10 | 9 | 7 | 2 | 90 |
| M75NL | 20 | 36 | 7 | 29 | 720 |
| M150S | 40 | 18 | 9 | 1 | 720 |
| M150L | 40 | 144 | 9 | 127 | 5760 |
| M300S | 40 | 18 | 9 | 1 | 720 |
| M300L | 40 | 144 | 9 | 127 | 5760 |
| M600S | 40 | 18 | 9 | 1 | 720 |
| M600L | 40 | 144 | 9 | 127 | 5760 |
| M1200S | 40 | 36 | 9 | 19 | 1440 |
| M1200L | 40 | 288 | 9 | 271 | 11520 |
| M2400S | 40 | 72 | 9 | 55 | 2880 |
| M2400L | 40 | 576 | 9 | 559 | 23040 |
| M4800S | 40 | 72 | 0 | 0 | 2880 (passthrough) |

### Symbol Repetition
| BPS | Repetition | Effective Rate |
|-----|------------|----------------|
| 75 | 32x | 2400/32 = 75 symbols/sec user data |
| 150 | 8x | With FEC: 150 bps |
| 300 | 4x | With FEC: 300 bps |
| 600 | 2x | With FEC: 600 bps |
| 1200 | 1x | With FEC: 1200 bps |
| 2400 | 1x | With FEC: 2400 bps |
| 4800 | 1x | No FEC: 4800 bps |

---

## Implementation Plan

### Phase 1: Core Infrastructure (HIGH PRIORITY)

#### 1.1 Fix Scrambler (CRITICAL)
Create `DataScramblerFixed` class that pre-computes 160 values and wraps:

```cpp
class DataScramblerFixed {
public:
    DataScramblerFixed() : offset_(0) {
        generate_sequence();
    }
    
    void reset() { offset_ = 0; }
    
    int next() {
        int val = sequence_[offset_];
        offset_ = (offset_ + 1) % 160;  // CRITICAL: wrap at 160
        return val;
    }
    
    int at(int pos) const {
        return sequence_[pos % 160];
    }
    
private:
    std::vector<int> sequence_;  // Pre-computed 160 values
    int offset_;
    
    void generate_sequence() {
        // 12-bit LFSR, seed 0xBAD, clock 8x per symbol
        int sreg[12] = {1,0,1,1,0,1,0,1,1,1,0,1};
        sequence_.resize(160);
        for (int i = 0; i < 160; i++) {
            for (int j = 0; j < 8; j++) {
                int c = sreg[11];
                for (int k = 11; k > 0; k--) sreg[k] = sreg[k-1];
                sreg[0] = c;
                sreg[6] ^= c; sreg[4] ^= c; sreg[1] ^= c;
            }
            sequence_[i] = (sreg[2] << 2) + (sreg[1] << 1) + sreg[0];
        }
    }
};
```

**Location**: `/src/modem/scrambler_fixed.h` (already created)
**Status**: Implementation exists, needs integration

#### 1.2 Create Unified Codec Class
New file: `/src/modem/m110a_codec.h`

```cpp
class M110ACodec {
public:
    explicit M110ACodec(ModeId mode);
    
    // TX Pipeline
    std::vector<complex_t> encode(const std::vector<uint8_t>& data);
    
    // RX Pipeline  
    std::vector<uint8_t> decode(const std::vector<complex_t>& symbols);
    
private:
    ModeId mode_;
    ModeConfig config_;
    DataScramblerFixed scrambler_;
    MultiModeInterleaver interleaver_;
    ConvEncoder encoder_;
    ViterbiDecoder decoder_;
};
```

### Phase 2: TX Pipeline Implementation

#### 2.1 Bit Conversion (LSB First)
```cpp
// Convert message bytes to bits (LSB first - CRITICAL!)
std::vector<int> bytes_to_bits_lsb(const std::vector<uint8_t>& bytes) {
    std::vector<int> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t byte : bytes) {
        for (int i = 0; i < 8; i++) {
            bits.push_back((byte >> i) & 1);  // LSB first!
        }
    }
    return bits;
}
```

#### 2.2 Complete TX Chain
```
Input Bytes 
  → bytes_to_bits_lsb() 
  → ConvEncoder.encode() 
  → MultiModeInterleaver.interleave()
  → Gray encode (MGD2/MGD3)
  → Scrambler add (mod 8)
  → Insert probe symbols
  → PSK modulation
  → Output symbols
```

### Phase 3: RX Pipeline Implementation

#### 3.1 Symbol Demodulation
```cpp
int decode_8psk_position(complex_t sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    return ((pos % 8) + 8) % 8;
}
```

#### 3.2 Complete RX Chain
```
Input symbols
  → Extract data symbols (skip probes)
  → Descramble (mod 8 subtraction)
  → Inverse Gray decode
  → Deinterleave
  → Viterbi decode (soft decisions)
  → bits_to_bytes_lsb()
  → Output bytes
```

#### 3.3 Soft Decision Generation
For Viterbi: bit 0 → +127, bit 1 → -127

```cpp
void tribit_to_soft_bits(int tribit, int8_t& b2, int8_t& b1, int8_t& b0) {
    b2 = (tribit & 4) ? -127 : 127;
    b1 = (tribit & 2) ? -127 : 127;
    b0 = (tribit & 1) ? -127 : 127;
}
```

### Phase 4: Mode-Specific Handlers

#### 4.1 BPSK Modes (75-600 bps)
- Use BPSK_SYMBOLS = {0, 4}
- Apply symbol repetition (32x, 8x, 4x, 2x)
- Special handling for 75 bps Walsh coding

#### 4.2 QPSK Mode (1200 bps)
- Use QPSK_SYMBOLS = {0, 2, 4, 6}
- Use MGD2 Gray code
- 20+20 frame structure

#### 4.3 8PSK Modes (2400-4800 bps)
- Use all 8 symbols
- Use MGD3 Gray code
- 32+16 frame structure
- 4800 bps: No FEC, passthrough interleaver

### Phase 5: Testing Strategy

#### 5.1 Loopback Tests (No Channel)
For each mode, verify encode→decode = identity:
```cpp
TEST(Loopback, M2400S) {
    M110ACodec codec(ModeId::M2400S);
    auto original = generate_test_message(54);
    auto symbols = codec.encode(original);
    auto decoded = codec.decode(symbols);
    EXPECT_EQ(original, decoded);
}
```

#### 5.2 Reference File Tests
For each reference PCM file:
1. Load PCM
2. Extract preamble & sync
3. Decode data symbols
4. Verify against expected "THE QUICK BROWN FOX..."

#### 5.3 Test Priority Order
1. M2400S (already working)
2. M1200S (next easiest, QPSK)
3. M600S (BPSK, no repetition complexity)
4. M300S, M150S (BPSK with repetition)
5. M75S (BPSK with Walsh coding)
6. Long interleave variants (M*L)
7. M4800S (uncoded)

---

## File Organization

### New Files to Create
```
src/modem/
├── m110a_codec.h           # Unified encode/decode class
├── scrambler_fixed.h       # Fixed scrambler (exists)
├── gray_code.h             # Gray code tables & functions
└── symbol_repetition.h     # Symbol repetition handling

test/
├── test_codec_loopback.cpp # Loopback tests all modes
├── test_reference_files.cpp # Test against reference PCMs
└── test_mode_specific.cpp  # Individual mode tests
```

### Files to Modify
```
src/modem/scrambler.h       # Add wrapping to RefScrambler
src/m110a/msdmt_decoder.h   # Integrate fixed scrambler
```

---

## Implementation Schedule

### Week 1: Core Fixes
- [ ] Integrate fixed scrambler into main codebase
- [ ] Create M110ACodec class skeleton
- [ ] Implement TX pipeline
- [ ] Verify M2400S still works after refactor

### Week 2: RX Pipeline & QPSK
- [ ] Implement RX pipeline
- [ ] Test M1200S decode
- [ ] Add soft decision generation
- [ ] Verify loopback for 8PSK and QPSK

### Week 3: BPSK Modes
- [ ] Implement symbol repetition
- [ ] Test M600S, M300S, M150S
- [ ] Investigate 75 bps Walsh coding
- [ ] Test M75S

### Week 4: Long Interleave & Polish
- [ ] Test all *L modes
- [ ] Test M4800S (uncoded)
- [ ] Performance optimization
- [ ] Documentation

---

## Key Learnings (from M2400S debugging)

1. **Scrambler wrapping is CRITICAL**: The TX pre-computes 160 values and wraps
2. **LSB-first bit ordering**: send_sync_octet_array() sends LSB first
3. **Absolute PSK, not differential**: Symbols are absolute phase positions
4. **Scrambler is ADDITIVE**: sym = (gray + scr) % 8, not XOR
5. **Interleaver state resets**: After loading full block, (row,col) returns to (0,0)
6. **Probe symbols**: Are pure scrambler output (data=0, gray=0)
7. **Frame alignment**: Probes allow scrambler sync verification

---

## Success Criteria

| Mode | Test File | Expected Result |
|------|-----------|-----------------|
| M2400S | tx_2400S_*.pcm | 54/54 chars ✅ |
| M1200S | tx_1200S_*.pcm | 54/54 chars |
| M600S | tx_600S_*.pcm | 54/54 chars |
| M300S | tx_300S_*.pcm | 54/54 chars |
| M150S | tx_150S_*.pcm | 54/54 chars |
| M75S | tx_75S_*.pcm | 54/54 chars |
| All *L modes | tx_*L_*.pcm | 54/54 chars |

Message: "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"
