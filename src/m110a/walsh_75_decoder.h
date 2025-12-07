/**
 * Walsh 75bps Decoder
 * 
 * Implements the complete Walsh decode algorithm for M75NS and M75NL modes
 * based on analysis of the reference MS-DMT implementation.
 * 
 * Key features:
 * - 4 Walsh patterns (MNS for normal, MES for exception blocks)
 * - sync_75_mask adaptive timing/channel estimation
 * - Gray code decoding
 * - Soft decision output
 */

#ifndef WALSH_75_DECODER_H
#define WALSH_75_DECODER_H

#include <vector>
#include <complex>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace m110a {

using complex_t = std::complex<float>;

/**
 * Walsh 75bps Decoder
 */
class Walsh75Decoder {
public:
    // MNS (Mode Normal Status) Walsh sequences
    static constexpr int MNS[4][32] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4,0,4},
        {0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4},
        {0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0,0,4,4,0}
    };
    
    // MES (Mode/Error Status) Walsh sequences
    static constexpr int MES[4][32] = {
        {0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4},
        {0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0},
        {0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0,0,0,4,4,4,4,0,0},
        {0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4}
    };
    
    // 8PSK constellation
    static constexpr float PSK8_I[8] = {1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f};
    static constexpr float PSK8_Q[8] = {0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f};
    
    static constexpr int SYNC_MASK_LENGTH = 32;
    static constexpr int SCRAMBLER_LENGTH = 160;
    
    /**
     * Decoded Walsh result
     */
    struct WalshResult {
        int data;           // 0-3 Walsh pattern detected
        float magnitude;    // Correlation magnitude
        float soft;         // Soft decision value
    };

    /**
     * Constructor
     * @param block_count_mod MES interval (45 for M75NS, 360 for M75NL)
     */
    explicit Walsh75Decoder(int block_count_mod = 45) 
        : block_count_mod_(block_count_mod)
        , block_count_(0)
        , scrambler_count_(0) {
        
        // Generate scrambler sequence
        generate_scrambler();
        
        // Initialize sync mask
        reset_sync_mask();
        
        // Pre-generate constellation symbols
        for (int i = 0; i < 8; i++) {
            psk8_[i] = complex_t(PSK8_I[i], PSK8_Q[i]);
        }
        
        // Pre-generate scrambled Walsh sequences (will be updated per decode)
        for (int d = 0; d < 4; d++) {
            for (int i = 0; i < 32; i++) {
                mns_seq_[d][i] = psk8_[MNS[d][i]];
                mes_seq_[d][i] = psk8_[MES[d][i]];
            }
        }
    }
    
    /**
     * Reset decoder state
     */
    void reset() {
        block_count_ = 0;
        scrambler_count_ = 0;
        reset_sync_mask();
    }
    
    /**
     * Decode a single Walsh symbol from 4800 Hz input
     * @param in Input symbols (64 samples at 4800 Hz, or 32 at 2400 Hz duplicated)
     * @return Decoded result with data, magnitude, and soft decision
     */
    WalshResult decode(const complex_t* in) {
        // Check if this is an MES block
        block_count_++;
        bool is_mes = (block_count_ == block_count_mod_);
        if (is_mes) {
            block_count_ = 0;
        }
        
        // Decode
        auto result = decode_internal(in, is_mes);
        
        // Advance scrambler
        scrambler_count_ = (scrambler_count_ + 32) % SCRAMBLER_LENGTH;
        
        return result;
    }
    
    /**
     * Decode a single Walsh symbol with explicit MES flag
     * @param in Input symbols (64 samples at 4800 Hz)
     * @param is_mes True if this is an MES block
     * @return Decoded result
     */
    WalshResult decode(const complex_t* in, bool is_mes) {
        auto result = decode_internal(in, is_mes);
        scrambler_count_ = (scrambler_count_ + 32) % SCRAMBLER_LENGTH;
        return result;
    }
    
    /**
     * Gray decode Walsh data to soft bits
     * @param data Walsh pattern index (0-3)
     * @param soft Soft decision magnitude
     * @param out Output vector to append 2 soft bits
     */
    static void gray_decode(int data, float soft, std::vector<int8_t>& out) {
        int s = static_cast<int>(soft * 127);
        s = std::max(-127, std::min(127, s));
        
        switch (data) {
            case 0: out.push_back(s);  out.push_back(s);  break;  // 00
            case 1: out.push_back(s);  out.push_back(-s); break;  // 01
            case 2: out.push_back(-s); out.push_back(-s); break;  // 11
            case 3: out.push_back(-s); out.push_back(s);  break;  // 10
        }
    }
    
    /**
     * Get current scrambler position
     */
    int scrambler_count() const { return scrambler_count_; }
    
    /**
     * Set scrambler position
     */
    void set_scrambler_count(int count) { scrambler_count_ = count % SCRAMBLER_LENGTH; }

private:
    int block_count_mod_;
    int block_count_;
    int scrambler_count_;
    
    int scrambler_bits_[SCRAMBLER_LENGTH];
    complex_t scrambler_seq_[SCRAMBLER_LENGTH];
    float sync_mask_[SYNC_MASK_LENGTH];
    complex_t psk8_[8];
    complex_t mns_seq_[4][32];
    complex_t mes_seq_[4][32];
    
    /**
     * Generate scrambler sequence
     * 12-bit LFSR: x^12 + x^7 + x^5 + x^2 + 1
     * Init: 101101011101
     * Matches reference t110a.cpp create_data_scrambler_seq()
     */
    void generate_scrambler() {
        int sreg[12];
        
        // Initialize exactly as reference
        sreg[0]  = 1;
        sreg[1]  = 0;
        sreg[2]  = 1;
        sreg[3]  = 1;
        sreg[4]  = 0;
        sreg[5]  = 1;
        sreg[6]  = 0;
        sreg[7]  = 1;
        sreg[8]  = 1;
        sreg[9]  = 1;
        sreg[10] = 0;
        sreg[11] = 1;
        
        for (int i = 0; i < SCRAMBLER_LENGTH; i++) {
            // Clock 8 times per output
            for (int j = 0; j < 8; j++) {
                int carry = sreg[11];
                // Shift and XOR exactly as reference - element by element
                sreg[11] = sreg[10];
                sreg[10] = sreg[9];
                sreg[9]  = sreg[8];
                sreg[8]  = sreg[7];
                sreg[7]  = sreg[6];
                sreg[6]  = sreg[5] ^ carry;  // XOR during shift
                sreg[5]  = sreg[4];
                sreg[4]  = sreg[3] ^ carry;  // XOR during shift
                sreg[3]  = sreg[2];
                sreg[2]  = sreg[1];
                sreg[1]  = sreg[0] ^ carry;  // XOR during shift
                sreg[0]  = carry;
            }
            // Output tribit from bits 0,1,2
            scrambler_bits_[i] = (sreg[2] << 2) | (sreg[1] << 1) | sreg[0];
            scrambler_seq_[i] = complex_t(PSK8_I[scrambler_bits_[i]], 
                                          PSK8_Q[scrambler_bits_[i]]);
        }
    }
    
    /**
     * Reset sync mask to uniform weights
     */
    void reset_sync_mask() {
        for (int i = 0; i < SYNC_MASK_LENGTH; i++) {
            sync_mask_[i] = 1.0f / SYNC_MASK_LENGTH;
        }
    }
    
    /**
     * Update sync mask with IIR filter
     */
    void update_sync_mask(const float* correlations) {
        for (int i = 0; i < SYNC_MASK_LENGTH; i++) {
            sync_mask_[i] = sync_mask_[i] * 0.50f + correlations[i] * 0.01f;
        }
    }
    
    /**
     * Scramble a Walsh sequence
     */
    void scramble_sequence(const complex_t* walsh, complex_t* out) {
        for (int i = 0; i < 32; i++) {
            int scr_idx = (i + scrambler_count_) % SCRAMBLER_LENGTH;
            // Complex multiply: walsh * scrambler
            out[i] = complex_t(
                walsh[i].real() * scrambler_seq_[scr_idx].real() - 
                walsh[i].imag() * scrambler_seq_[scr_idx].imag(),
                walsh[i].real() * scrambler_seq_[scr_idx].imag() + 
                walsh[i].imag() * scrambler_seq_[scr_idx].real()
            );
        }
    }
    
    /**
     * Match sequence with i*2 spacing (for 4800 Hz input)
     * Computes conjugate correlation magnitude squared
     */
    float match_sequence(const complex_t* in, const complex_t* seq) {
        complex_t sum(0, 0);
        for (int i = 0; i < 32; i++) {
            // Conjugate multiply: in * conj(seq)
            sum += complex_t(
                in[i*2].real() * seq[i].real() + in[i*2].imag() * seq[i].imag(),
                in[i*2].imag() * seq[i].real() - in[i*2].real() * seq[i].imag()
            );
        }
        return std::norm(sum);
    }
    
    /**
     * Accumulate Walsh symbol with sync mask weighting
     * Performs 32 sliding correlations and returns weighted sum
     */
    float accumulate_symbol(const complex_t* in, const complex_t* expected, float* out) {
        float total = 0;
        for (int offset = 0; offset < SYNC_MASK_LENGTH; offset++) {
            out[offset] = match_sequence(&in[offset], expected);
            total += out[offset] * sync_mask_[offset];
        }
        return total;
    }
    
    /**
     * Internal decode function
     */
    WalshResult decode_internal(const complex_t* in, bool is_mes) {
        complex_t scrambled[32];
        float correlations[4][SYNC_MASK_LENGTH];
        float magnitudes[4];
        float total_mag = 0;
        
        // Select pattern set
        const complex_t (*patterns)[32] = is_mes ? mes_seq_ : mns_seq_;
        
        // Correlate against all 4 patterns
        for (int d = 0; d < 4; d++) {
            scramble_sequence(patterns[d], scrambled);
            magnitudes[d] = accumulate_symbol(in, scrambled, correlations[d]);
            total_mag += magnitudes[d];
        }
        
        // Find best match
        int best = 0;
        for (int d = 1; d < 4; d++) {
            if (magnitudes[d] > magnitudes[best]) {
                best = d;
            }
        }
        
        // Update sync mask with winning pattern
        update_sync_mask(correlations[best]);
        
        // Calculate soft decision
        float soft = (total_mag > 0) ? std::sqrt(magnitudes[best] / total_mag) : 0;
        
        return {best, magnitudes[best], soft};
    }
};

// Static member definitions
constexpr int Walsh75Decoder::MNS[4][32];
constexpr int Walsh75Decoder::MES[4][32];
constexpr float Walsh75Decoder::PSK8_I[8];
constexpr float Walsh75Decoder::PSK8_Q[8];

} // namespace m110a

#endif // WALSH_75_DECODER_H
