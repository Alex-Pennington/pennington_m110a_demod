#ifndef M110A_FRAME_DFE_H
#define M110A_FRAME_DFE_H

#include "equalizer/dfe.h"
#include "modem/scrambler_fixed.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace m110a {

/// True for the probe of the last two frames of an interleaver block.
/// MIL-STD-188-110C 5.3.2.3.7.1.2: the two known-symbol patterns before
/// each new interleaver block carry D1 and D2 (Walsh patterns) instead of
/// zeros. A transmitter that follows it (the Harris RT-1694D does) sends
/// them; this modem's TX sends zeros. These probes are left out of probe
/// training and of the quality figure, so either kind of transmitter is
/// read the same way.
inline bool block_boundary_probe(int frame, int block_frames) {
    return block_frames > 2 && (frame % block_frames) >= block_frames - 2;
}

/// Output of frame_dfe_equalize: the equalized stream (probes passed through),
/// and per frame inside the quality span its end (symbols), and the probe error
/// and reference power of the DFE's a-priori outputs on that frame's probes.
struct FrameDfeOutput {
    std::vector<complex_t> symbols;
    std::vector<std::array<double, 3>> probe_frames;
};

/**
 * Apply probe-aided channel equalization using the DFE
 *
 * Frame structure: [unknown_len data][known_len probes], e.g. [32 data][16 probes]
 * Strategy:
 * 1. Pretrain DFE on preamble (known sequence)
 * 2. Enter the data with the delay lines holding the last preamble symbols
 * 3. Equalize each frame's data, then update on its probes
 *
 * @param symbols Data symbols, data and probes interleaved, from data symbol 0
 * @param block_frames Frames per interleaver block, 0 when unknown
 * @param preamble_symbols First preamble frame (480 symbols), for pretraining
 * @param pre_data_symbols The 32 symbols just before data symbol 0 (zero segment)
 * @param quality_span_symbols Frames ending within this many symbols are scored
 */
inline FrameDfeOutput frame_dfe_equalize(
    const std::vector<complex_t>& symbols,
    int unknown_len, int known_len,
    int block_frames,
    const std::vector<complex_t>& preamble_symbols = {},
    bool use_nlms = false,
    const std::vector<complex_t>& pre_data_symbols = {},
    int quality_span_symbols = 1440) {
    
    // 8-PSK constellation
    static const std::array<complex_t, 8> PSK8 = {{
        complex_t( 1.000f,  0.000f),
        complex_t( 0.707f,  0.707f),
        complex_t( 0.000f,  1.000f),
        complex_t(-0.707f,  0.707f),
        complex_t(-1.000f,  0.000f),
        complex_t(-0.707f, -0.707f),
        complex_t( 0.000f, -1.000f),
        complex_t( 0.707f, -0.707f)
    }};
    
    // Probes are tribit 0 -> MGD3[0] = 0
    static const int PROBE_GRAY = 0;
    
    // Preamble scrambling sequence
    static const std::array<uint8_t, 32> pscramble = {
        7, 4, 3, 0, 5, 1, 5, 0, 2, 2, 1, 1, 5, 7, 4, 3,
        5, 0, 2, 6, 2, 1, 6, 2, 0, 0, 5, 0, 5, 2, 6, 6
    };
    
    // Common preamble pattern (D values)
    static const std::array<uint8_t, 9> p_c_seq = {0, 1, 3, 0, 1, 3, 1, 2, 0};
    
    // PSK symbol patterns
    static const std::array<std::array<uint8_t, 8>, 8> psymbol = {{
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, 4, 0, 4, 0, 4, 0, 4},
        {0, 0, 4, 4, 0, 0, 4, 4},
        {0, 4, 4, 0, 0, 4, 4, 0},
        {0, 0, 0, 0, 4, 4, 4, 4},
        {0, 4, 0, 4, 4, 0, 4, 0},
        {0, 0, 4, 4, 4, 4, 0, 0},
        {0, 4, 4, 0, 4, 0, 0, 4}
    }};
    
    // Configure DFE
    DFE::Config dfe_cfg;
    dfe_cfg.ff_taps = 11;
    dfe_cfg.fb_taps = 5;
    dfe_cfg.use_nlms = use_nlms;
    
    if (use_nlms) {
        // NLMS: mu gets normalized by input power, so use larger values
        // Typical NLMS mu range: 0.1 to 1.0
        dfe_cfg.mu_ff = 0.3f;    // Aggressive, but normalized
        dfe_cfg.mu_fb = 0.15f;   
        dfe_cfg.nlms_delta = 0.01f;  // Regularization to prevent div-by-zero
    } else {
        // Standard LMS: conservative fixed step sizes
        dfe_cfg.mu_ff = 0.005f;  
        dfe_cfg.mu_fb = 0.002f;
    }
    dfe_cfg.leak = 0.0001f;

    // The feed-forward taps span the symbols -5 to +5 around the one decided, so
    // they cancel precursor ISI as well as postcursor: the DFE is fed each input
    // AHEAD symbols early, and its lookahead puts the unity center tap on the
    // symbol decided. An FT-897D SSB filter (top of the 2400-baud band cut from
    // 2300 Hz, 0.8-1.8 ms of edge delay) gave a 0.40 precursor, and 2400S decoded
    // at 6 dB with wrong bytes when the taps saw no later symbol.
    dfe_cfg.lookahead = dfe_cfg.ff_taps / 2;
    const size_t AHEAD = static_cast<size_t>(dfe_cfg.lookahead);

    DFE dfe(dfe_cfg);
    auto ahead = [AHEAD](const std::vector<complex_t>& v, size_t i, size_t n) {
        std::vector<complex_t> w(n, complex_t(0.0f, 0.0f));
        for (size_t k = 0; k < n && i + AHEAD + k < v.size(); k++) w[k] = v[i + AHEAD + k];
        return w;
    };
    // Start a known stretch rx[0..] / ref[0..] with both delay lines full: the
    // inputs before rx[AHEAD] go into the feed-forward line, and the first AHEAD
    // references into the feedback line against rx[AHEAD..2*AHEAD-1]. Training
    // then starts at ref[AHEAD] (returned), with input rx[2*AHEAD].
    auto prime_known = [&dfe, AHEAD](const std::vector<complex_t>& rx,
                                     const std::vector<complex_t>& ref) {
        dfe.clear_lines();
        for (size_t k = 0; k < AHEAD; k++) dfe.push(rx[k]);
        for (size_t k = 0; k < AHEAD; k++) dfe.prime(rx[AHEAD + k], ref[k]);
        return AHEAD;
    };
    
    // ========================================
    // Preamble Pretraining
    // ========================================
    if (!preamble_symbols.empty()) {
        // Generate expected common preamble (first 288 symbols)
        // This is the most reliable part for channel estimation
        int pretrain_len = std::min(288, static_cast<int>(preamble_symbols.size()));
        
        std::vector<complex_t> preamble_ref;
        preamble_ref.reserve(pretrain_len);
        
        int scram_idx = 0;
        for (int i = 0; i < 9 && static_cast<int>(preamble_ref.size()) < pretrain_len; i++) {
            uint8_t d_val = p_c_seq[i];
            for (int j = 0; j < 32 && static_cast<int>(preamble_ref.size()) < pretrain_len; j++) {
                uint8_t base = psymbol[d_val][j % 8];
                uint8_t scrambled = (base + pscramble[scram_idx % 32]) % 8;
                preamble_ref.push_back(PSK8[scrambled]);
                scram_idx++;
            }
        }
        
        // LMS: the least-squares taps over the common preamble, which the LMS would
        // need a 20x step to reach and would then carry that step's noise into the
        // data (on a flat channel at low SNR, 0.3-0.6 dB of decode quality).
        // NLMS: train on the preamble in chunks, two passes.
        const bool fitted = !use_nlms &&
            dfe.fit_least_squares(preamble_symbols, preamble_ref, static_cast<int>(AHEAD), pretrain_len);
        int chunk_size = 32;
        for (int pass = 0; pass < (fitted ? 0 : 2); pass++) {
            const int first = static_cast<int>(prime_known(preamble_symbols, preamble_ref));
            for (int i = first; i < pretrain_len; i += chunk_size) {
                const int n = std::min(chunk_size, pretrain_len - i);
                std::vector<complex_t> rx_chunk = ahead(preamble_symbols, i, n);
                std::vector<complex_t> ref_chunk(
                    preamble_ref.begin() + i,
                    preamble_ref.begin() + i + n);
                
                dfe.train(rx_chunk, ref_chunk);
            }
        }
    }

    // Enter the data with the delay lines holding what precedes it: the last
    // preamble frame's zero segment, each symbol PSK8[pscramble[i]] in every mode,
    // trained on at the tracking step. Its last AHEAD feed-forward inputs are the
    // first data symbols. Without it the data starts from an empty past.
    if (!preamble_symbols.empty() && pre_data_symbols.size() >= 2 * AHEAD) {
        std::vector<complex_t> rx = pre_data_symbols;
        rx.insert(rx.end(), symbols.begin(), symbols.begin() + std::min(AHEAD, symbols.size()));
        rx.resize(pre_data_symbols.size() + AHEAD, complex_t(0.0f, 0.0f));
        std::vector<complex_t> ref;
        for (size_t i = 0; i < pre_data_symbols.size(); i++)
            ref.push_back(PSK8[pscramble[i % 32]]);
        const size_t first = prime_known(rx, ref);
        dfe.train(std::vector<complex_t>(rx.begin() + first + AHEAD, rx.end()),
                  std::vector<complex_t>(ref.begin() + first, ref.end()));
    } else {
        dfe.clear_lines();
        for (size_t k = 0; k < AHEAD && k < symbols.size(); k++) dfe.push(symbols[k]);
    }
    
    // ========================================
    // Data Frame Processing
    // ========================================
    DataScramblerFixed scrambler;
    
    int pattern_len = unknown_len + known_len;
    FrameDfeOutput result;
    std::vector<complex_t>& output = result.symbols;
    output.reserve(symbols.size());
    
    size_t idx = 0;
    int frame = 0;

    while (idx + pattern_len <= symbols.size()) {
        // Get current frame data and probes
        std::vector<complex_t> data_in = ahead(symbols, idx, unknown_len);
        
        std::vector<complex_t> probe_in(
            symbols.begin() + idx + unknown_len,
            symbols.begin() + idx + pattern_len);
        const std::vector<complex_t> probe_eq = ahead(symbols, idx + unknown_len, known_len);
        
        // Generate probe reference using synchronized scrambler
        scrambler.reset();
        int scr_pos = frame * pattern_len + unknown_len;
        for (int i = 0; i < scr_pos; i++) scrambler.next();
        
        std::vector<complex_t> probe_ref(known_len);
        for (int i = 0; i < known_len; i++) {
            int scr = scrambler.next();
            int sym_idx = (PROBE_GRAY + scr) & 7;
            probe_ref[i] = PSK8[sym_idx];
        }
        
        // With preamble pretraining, DFE is already initialized
        // So we can equalize immediately on first frame
        if (frame == 0 && preamble_symbols.empty()) {
            // No preamble: train on the first probe (its feed-forward past is the
            // data before it, its feedback past unknown), then go back to the data
            // start with empty lines and run the frame through in order.
            dfe.clear_lines();
            for (size_t k = 0; k < 2 * AHEAD; k++)
                dfe.push(idx + unknown_len + k >= AHEAD && idx + unknown_len + k - AHEAD < symbols.size()
                         ? symbols[idx + unknown_len + k - AHEAD] : complex_t(0.0f, 0.0f));
            dfe.train(probe_eq, probe_ref);
            dfe.clear_lines();
            for (size_t k = 0; k < AHEAD && k < symbols.size(); k++) dfe.push(symbols[k]);
            dfe.equalize(data_in, output);
            dfe.train(probe_eq, probe_ref);
        } else {
            // With preamble or subsequent frames: equalize first, then update.
            // A block-boundary probe may carry D1/D2 instead of the zero probe:
            // no adaptation, and each symbol decided between the two candidates,
            // so the feedback line stays a line of decisions.
            dfe.equalize(data_in, output);
            if (!block_boundary_probe(frame, block_frames))
                {
                // Probe error of the DFE's output before it adapts to each probe,
                // per frame while the frame lies inside the quality span
                const float mse = dfe.train(probe_eq, probe_ref);
                const size_t frame_end = idx + pattern_len;
                if (frame_end <= static_cast<size_t>(quality_span_symbols) && !probe_ref.empty()) {
                    double ref_pow = 0.0;
                    for (const auto& r : probe_ref) ref_pow += std::norm(r);
                    result.probe_frames.push_back({static_cast<double>(frame_end),
                                                   static_cast<double>(mse) * probe_ref.size(), ref_pow});
                }
            }
            else
                for (int i = 0; i < known_len; i++) dfe.process_antipodal(probe_eq[i], probe_ref[i]);
        }
        
        // Pass through original probes
        output.insert(output.end(), probe_in.begin(), probe_in.end());
        
        idx += pattern_len;
        frame++;
    }
    
    // Remaining symbols - pass through
    if (idx < symbols.size()) {
        output.insert(output.end(), symbols.begin() + idx, symbols.end());
    }
    
    return result;
}

} // namespace m110a

#endif // M110A_FRAME_DFE_H
