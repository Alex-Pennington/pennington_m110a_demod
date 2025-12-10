# Detailed Implementation Specs

**Created:** Session 18 (2025-12-07)

---

## 1. EOM (End of Message) Marker

### Overview
Add clean transmission termination per MIL-STD-188-110A spec. EOM allows receiver to detect end of data without timeout.

### Specification (MIL-STD-188-110A Section 5.4)
- EOM consists of 4 repeated flush frames
- Each flush frame: all-zeros data with standard probe pattern
- Receiver detects EOM by correlating against known flush pattern

### Implementation Tasks

#### TX Side
- [ ] Add `generate_eom()` to `MSDMTEncoder`
  ```cpp
  std::vector<complex_t> generate_eom(int mode_index);
  ```
- [ ] EOM structure: 4 frames Ã— (unknown_len + known_len) symbols
- [ ] Data portion: scrambled zeros (tribit 0 â†’ gray â†’ scramble)
- [ ] Probe portion: normal probe pattern (already known)
- [ ] `TxConfig.include_eom` flag already exists - wire it up

#### RX Side
- [ ] Add `detect_eom()` to `BrainDecoder`
  ```cpp
  bool detect_eom(const std::vector<complex_t>& symbols, int frame_start);
  ```
- [ ] Correlate against expected EOM pattern
- [ ] Threshold: correlation > 0.8 for 3+ consecutive frames
- [ ] Return early from decode when EOM detected
- [ ] Add `RxResult.eom_detected` flag

#### Testing
- [ ] Test EOM generation matches expected pattern
- [ ] Test EOM detection with clean signal
- [ ] Test EOM detection with AWGN @ 15 dB
- [ ] Test partial data + EOM (short message)

### Files to Modify
- `src/m110a/msdmt_encoder.h` - Add generate_eom()
- `src/m110a/brain_decoder.h` - Add detect_eom()
- `api/modem_tx.cpp` - Call generate_eom() when include_eom=true
- `api/modem_rx.cpp` - Call detect_eom() during decode
- `api/modem_types.h` - Add eom_detected to RxResult

### Effort Estimate
- Implementation: 2-3 hours
- Testing: 1 hour

---

## 2. Streaming RX (Real-time Processing)

### Overview
Enable sample-by-sample processing for real-time applications. Current implementation requires complete signal buffer.

### Requirements
- Process audio in chunks (e.g., 256-1024 samples)
- Maintain state between calls
- Output decoded bytes as they become available
- Handle preamble detection across chunk boundaries

### Implementation Tasks

#### State Machine
- [ ] Define RX states:
  ```cpp
  enum class StreamState {
      SEARCHING,      // Looking for preamble
      SYNCHRONIZING,  // Fine-tuning timing
      RECEIVING,      // Decoding data frames
      EOM_DETECTED,   // End of message received
      IDLE            // Ready for next transmission
  };
  ```

#### Streaming Preamble Detection
- [ ] Create `StreamingPreambleDetector` class
  ```cpp
  class StreamingPreambleDetector {
  public:
      void push_samples(const float* samples, int count);
      bool preamble_found() const;
      int get_start_sample() const;
      float get_correlation() const;
  private:
      RingBuffer<float> sample_buffer_;
      int correlation_state_;
  };
  ```
- [ ] Sliding correlation with overlap
- [ ] Peak detection with hysteresis

#### Streaming Demodulator
- [ ] Create `StreamingDemodulator` class
  ```cpp
  class StreamingDemodulator {
  public:
      void push_samples(const float* samples, int count);
      std::vector<complex_t> get_symbols();  // Returns available symbols
  private:
      NCO carrier_nco_;
      FIRFilter matched_filter_;
      TimingRecovery timing_;
  };
  ```
- [ ] Symbol-by-symbol output
- [ ] Timing recovery with Gardner algorithm

#### Streaming Decoder
- [ ] Create `StreamingDecoder` class
  ```cpp
  class StreamingDecoder {
  public:
      void push_symbols(const complex_t* symbols, int count);
      std::vector<uint8_t> get_bytes();  // Returns decoded bytes
      bool is_complete() const;
  private:
      std::vector<complex_t> symbol_buffer_;
      ViterbiDecoder viterbi_;
      Deinterleaver deinterleaver_;
  };
  ```
- [ ] Frame-boundary detection
- [ ] Incremental Viterbi (output when traceback ready)

#### API
- [ ] Create `StreamingRX` class
  ```cpp
  class StreamingRX {
  public:
      explicit StreamingRX(const RxConfig& config);
      
      // Push audio samples (can be called multiple times)
      void push_samples(const float* samples, int count);
      
      // Get decoded data (returns bytes available so far)
      std::vector<uint8_t> get_data();
      
      // Check state
      StreamState state() const;
      bool is_receiving() const;
      bool is_complete() const;
      
      // Stats
      float snr_estimate() const;
      float freq_offset() const;
  };
  ```

#### Testing
- [ ] Test with chunked input (256 sample chunks)
- [ ] Test preamble split across chunks
- [ ] Test frame boundary across chunks
- [ ] Verify identical output to batch decoder
- [ ] Latency measurement (samples to first byte)

### Files to Create
```
src/streaming/
â”œâ”€â”€ ring_buffer.h
â”œâ”€â”€ streaming_preamble.h
â”œâ”€â”€ streaming_demod.h
â”œâ”€â”€ streaming_decoder.h
â””â”€â”€ streaming_state.h

api/
â”œâ”€â”€ streaming_rx.h
â””â”€â”€ streaming_rx.cpp

test/
â””â”€â”€ test_streaming.cpp
```

### Effort Estimate
- Ring buffer + state machine: 2 hours
- Streaming preamble: 3 hours
- Streaming demod: 3 hours
- Streaming decoder: 4 hours
- Integration + testing: 4 hours
- **Total: ~16 hours**

---

## 3. Adaptive LMS Step Size

### Overview
Auto-tune DFE step size (Î¼) based on estimated channel conditions. Fixed Î¼ is suboptimal: too large diverges on fading, too small tracks poorly.

### Algorithm Options

#### Option A: Normalized LMS (NLMS) - RECOMMENDED
```cpp
Î¼_eff = Î¼ / (Î´ + ||x||Â²)
```
- Normalizes by input power
- Simple, robust
- Standard choice for adaptive filters

#### Option B: Variable Step-Size LMS (VS-LMS)
```cpp
if (|e[n]| > |e[n-1]|)
    Î¼[n] = Î± * Î¼[n-1]      // Error growing, reduce step
else
    Î¼[n] = min(Î¼_max, Î¼[n-1] / Î±)  // Error shrinking, increase step
```
- Adapts based on error trend
- Good for time-varying channels

#### Option C: SNR-Based Adaptation
```cpp
snr_est = signal_power / noise_power;
Î¼ = Î¼_base * sigmoid(snr_est - snr_threshold);
```
- Higher SNR â†’ larger step (faster tracking)
- Lower SNR â†’ smaller step (more averaging)

### Implementation Tasks

#### Core Algorithm (NLMS)
- [ ] Add NLMS option to DFE::Config
  ```cpp
  struct DFE::Config {
      // ... existing ...
      bool use_nlms = true;
      float nlms_delta = 0.001f;  // Regularization
  };
  ```
- [ ] Modify `DFE::train()` to use normalized step:
  ```cpp
  void train(const std::vector<complex_t>& input,
             const std::vector<complex_t>& desired) {
      for (size_t n = 0; n < input.size(); n++) {
          // Compute output and error
          complex_t y = compute_output(input, n);
          complex_t e = desired[n] - y;
          
          // Compute input power for normalization
          float input_power = 0;
          for (int k = 0; k < ff_taps_; k++) {
              if (n >= k) input_power += std::norm(input[n-k]);
          }
          
          // Normalized step size
          float mu_eff = config_.use_nlms 
              ? config_.mu_ff / (config_.nlms_delta + input_power)
              : config_.mu_ff;
          
          // Update taps
          for (int k = 0; k < ff_taps_; k++) {
              if (n >= k) {
                  ff_taps_[k] += mu_eff * e * std::conj(input[n-k]);
              }
          }
      }
  }
  ```

#### SNR Estimation
- [ ] Add probe-based SNR estimator
  ```cpp
  float estimate_snr(const std::vector<complex_t>& received,
                     const std::vector<complex_t>& expected) {
      float signal_power = 0, error_power = 0;
      for (size_t i = 0; i < received.size(); i++) {
          signal_power += std::norm(expected[i]);
          error_power += std::norm(received[i] - expected[i]);
      }
      return signal_power / (error_power + 1e-10f);
  }
  ```
- [ ] Smooth over multiple frames using exponential average

#### Step Size Scheduler (Optional, for VS-LMS)
- [ ] Create `StepSizeScheduler` class
  ```cpp
  class StepSizeScheduler {
  public:
      StepSizeScheduler(float initial_mu, float alpha = 0.97f);
      
      void update(float error_magnitude);
      float get_mu() const { return mu_; }
      
  private:
      float mu_;
      float mu_min_, mu_max_;
      float alpha_;
      float prev_error_;
  };
  ```

#### Integration
- [ ] Update `apply_dfe_equalization()` in modem_rx.cpp:
  ```cpp
  // Enable NLMS by default
  DFE::Config dfe_cfg;
  dfe_cfg.use_nlms = true;
  dfe_cfg.nlms_delta = 0.001f;
  dfe_cfg.mu_ff = 0.1f;   // Can be larger with NLMS
  dfe_cfg.mu_fb = 0.05f;
  ```

#### Testing
- [ ] Test: NLMS vs fixed on AWGN @ various SNR
- [ ] Test: NLMS vs fixed on CCIR Good
- [ ] Test: NLMS vs fixed on CCIR Moderate
- [ ] Test: Convergence speed comparison
- [ ] Test: Ensure no regression on existing tests

### Files to Modify
- `src/equalizer/dfe.h` - Add NLMS option, modify train()
- `api/modem_rx.cpp` - Enable NLMS, optionally add SNR estimation
- `test/test_adaptive_lms.cpp` - New test file

### Expected Improvement
| Channel | Fixed LMS | NLMS | Improvement |
|---------|-----------|------|-------------|
| CCIR Good @ 18 dB | 12% BER | 8% BER | ~30% |
| CCIR Moderate | 50% BER | 45% BER | ~10% |
| Varying SNR | Unstable | Stable | Robustness |

### Effort Estimate
- NLMS implementation: 1 hour
- SNR estimation: 1 hour
- Testing: 2 hours
- **Total: ~4 hours**

---

## 4. Documentation Cleanup

### Overview
Consolidate and update documentation to reflect current implementation.

### Current State
- `docs/TX_CHAIN.md` - Exists but is actually RX docs
- `docs/M75_DEVELOPMENT.md` - Partially complete
- `docs/TODO.md` - Outdated
- Various notes scattered in code comments
- No README.md in docs/

### Documentation Structure

```
docs/
â”œâ”€â”€ README.md           # Project overview & quick start
â”œâ”€â”€ API.md              # API reference with examples
â”œâ”€â”€ TX_CHAIN.md         # Transmitter documentation (NEW)
â”œâ”€â”€ RX_CHAIN.md         # Receiver documentation (rename current)
â”œâ”€â”€ EQUALIZERS.md       # Equalizer theory & usage
â”œâ”€â”€ PROTOCOL.md         # MIL-STD-188-110A protocol details
â”œâ”€â”€ TESTING.md          # Test documentation
â”œâ”€â”€ TODO.md             # Active TODO tracker
â”œâ”€â”€ TODO_DETAILED.md    # This file - implementation specs
â””â”€â”€ M75_DEVELOPMENT.md  # M75 Walsh mode notes (existing)

examples/
â”œâ”€â”€ simple_tx.cpp       # Basic transmit example
â”œâ”€â”€ simple_rx.cpp       # Basic receive example
â”œâ”€â”€ loopback.cpp        # TX â†’ RX loopback test
â””â”€â”€ file_decode.cpp     # Decode from WAV/PCM file
```

### Tasks

#### docs/README.md
- [ ] Project description
- [ ] Feature list
- [ ] Supported modes table
- [ ] Quick start (5-minute guide)
- [ ] Build instructions
- [ ] Dependencies
- [ ] License

#### docs/API.md
- [ ] ModemTX class reference
  - Constructor options
  - encode() method
  - Configuration examples
- [ ] ModemRX class reference
  - Constructor options
  - decode() method
  - Equalizer selection
- [ ] Error handling
  - Result<T> pattern
  - Error codes
- [ ] Code examples for common tasks

#### docs/TX_CHAIN.md (NEW - actual TX docs)
- [ ] Encoding pipeline overview
- [ ] Block diagram (ASCII art)
- [ ] FEC encoding details
- [ ] Interleaver operation
- [ ] Symbol mapping
- [ ] Preamble generation
- [ ] Probe insertion
- [ ] Modulation

#### docs/RX_CHAIN.md (rename from TX_CHAIN.md)
- [ ] Receiving pipeline overview
- [ ] Block diagram
- [ ] Preamble detection
- [ ] Mode detection
- [ ] Symbol extraction
- [ ] Phase tracking
- [ ] Equalization (DFE, MLSE)
- [ ] FEC decoding
- [ ] Deinterleaving

#### docs/EQUALIZERS.md
- [ ] Why equalization matters
- [ ] DFE (Decision Feedback Equalizer)
  - Theory
  - LMS adaptation
  - Preamble pretraining
  - Configuration
- [ ] MLSE (Maximum Likelihood Sequence Estimation)
  - Viterbi algorithm
  - Channel estimation
  - L2 vs L3 tradeoffs
- [ ] Phase Tracking
  - When it applies
  - PLL parameters
- [ ] Performance comparison table
- [ ] Recommendations by channel type

#### docs/PROTOCOL.md
- [ ] MIL-STD-188-110A overview
- [ ] Frame structure
- [ ] Preamble format (with diagrams)
- [ ] Data frame format
- [ ] Probe insertion points
- [ ] Interleaver patterns
- [ ] Scrambler polynomial
- [ ] Mode parameters table

#### docs/TESTING.md
- [ ] Test suite overview
- [ ] How to run tests
- [ ] Channel models
  - AWGN
  - Watterson (CCIR Good/Moderate/Poor)
- [ ] Expected performance benchmarks
- [ ] Adding new tests

#### Examples
- [ ] `examples/simple_tx.cpp`
  ```cpp
  // Encode 100 bytes at 2400 bps, save to WAV
  ```
- [ ] `examples/simple_rx.cpp`
  ```cpp
  // Decode from WAV file, print bytes
  ```
- [ ] `examples/loopback.cpp`
  ```cpp
  // TX â†’ channel â†’ RX test with BER
  ```
- [ ] `examples/file_decode.cpp`
  ```cpp
  // Decode real signal from file
  ```

#### Code Comment Cleanup
- [ ] Add file headers to all .h/.cpp files
- [ ] Document non-obvious algorithms
- [ ] Add MIL-STD section references
- [ ] Remove stale TODO comments
- [ ] Consistent formatting

### Effort Estimate
- README.md: 1 hour
- API.md: 2 hours
- TX_CHAIN.md: 2 hours
- RX_CHAIN.md: 1 hour (mostly rename + review)
- EQUALIZERS.md: 2 hours
- PROTOCOL.md: 2 hours
- TESTING.md: 1 hour
- Examples: 2 hours
- Code comments: 2 hours
- **Total: ~15 hours**

---

## Priority Order

| Priority | Item | Effort | Impact |
|----------|------|--------|--------|
| 1 | EOM Marker | 3-4 hrs | Protocol completeness |
| 2 | Adaptive LMS | 4 hrs | Performance improvement |
| 3 | Documentation | 15 hrs | Maintainability |
| 4 | Streaming RX | 16 hrs | Real-time capability |

### Recommended Approach
1. Start with **EOM** (quick win, completes protocol)
2. Then **Adaptive LMS** (measurable improvement)
3. **Documentation** can be done incrementally
4. **Streaming RX** is a larger project for later

---

## Session 18 Summary

**Completed:**
- DFE with preamble pretraining (60% BER improvement)
- MLSE initialization bugs fixed
- Adaptive phase tracking
- All 41 tests passing

**Test Results:**
- Brain Modem compatibility: 11/11 âœ“
- PCM loopback: 11/11 âœ“
- Watterson HF: 19/19 âœ“
