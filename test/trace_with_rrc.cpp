/**
 * Trace data with RRC filter (like MSDMTDecoder)
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include "m110a/msdmt_preamble.h"

using namespace m110a;
using namespace std;

vector<float> read_pcm(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) return {};
    file.seekg(0, ios::end);
    size_t size = file.tellg();
    file.seekg(0);
    size_t num_samples = size / 2;
    vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), size);
    vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = raw[i] / 32768.0f;
    }
    return samples;
}

int decode_8psk_position(complex<float> sym) {
    float angle = atan2(sym.imag(), sym.real());
    int pos = static_cast<int>(round(angle * 4.0f / M_PI));
    pos = ((pos % 8) + 8) % 8;
    return pos;
}

// Generate RRC filter like MSDMTDecoder
vector<float> make_rrc(float alpha, int span, int sps) {
    int ntaps = span * sps + 1;
    vector<float> h(ntaps);
    
    for (int i = 0; i < ntaps; i++) {
        float t = (i - ntaps/2.0f) / sps;
        if (fabs(t) < 1e-6f) {
            h[i] = 1.0f - alpha + 4.0f * alpha / M_PI;
        } else if (fabs(fabs(t) - 1.0f/(4.0f*alpha)) < 1e-6f) {
            h[i] = alpha/sqrtf(2.0f) * ((1+2.0f/M_PI)*sinf(M_PI/(4*alpha)) + 
                                        (1-2.0f/M_PI)*cosf(M_PI/(4*alpha)));
        } else {
            float num = sinf(M_PI*t*(1-alpha)) + 4*alpha*t*cosf(M_PI*t*(1+alpha));
            float den = M_PI*t*(1 - powf(4*alpha*t, 2));
            h[i] = num / den;
        }
    }
    
    // Normalize
    float sum = 0;
    for (auto v : h) sum += v;
    for (auto& v : h) v /= sum;
    
    return h;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    cout << "Samples: " << samples.size() << endl;
    
    // Downconvert and RRC filter (like MSDMTDecoder)
    int sps = 20;
    float fc = 1800.0f, fs = 48000.0f;
    float alpha = 0.35f;
    int span = 6;
    
    auto rrc = make_rrc(alpha, span, sps);
    int half = rrc.size() / 2;
    
    // Downconvert
    vector<complex<float>> bb(samples.size());
    float phase = 0, phase_inc = 2 * M_PI * fc / fs;
    for (size_t i = 0; i < samples.size(); i++) {
        bb[i] = complex<float>(samples[i] * cos(phase), -samples[i] * sin(phase));
        phase += phase_inc;
        if (phase > 2*M_PI) phase -= 2*M_PI;
    }
    
    // Apply RRC filter
    vector<complex<float>> filtered(bb.size());
    for (size_t i = 0; i < bb.size(); i++) {
        complex<float> sum(0, 0);
        for (size_t j = 0; j < rrc.size(); j++) {
            if (i >= j) {
                sum += bb[i - j] * rrc[j];
            }
        }
        filtered[i] = sum;
    }
    
    int preamble_start = 257;
    
    // Verify preamble 
    vector<int> expected;
    for (int i = 0; i < 288; i++) {
        uint8_t d_val = msdmt::p_c_seq[i / 32];
        uint8_t base = msdmt::psymbol[d_val][i % 8];
        uint8_t scr = msdmt::pscramble[i % 32];
        expected.push_back((base + scr) % 8);
    }
    
    int matches = 0;
    for (int i = 0; i < 288; i++) {
        int idx = preamble_start + i * sps;
        if (idx < (int)filtered.size()) {
            int rcv = decode_8psk_position(filtered[idx]);
            if (rcv == expected[i]) matches++;
        }
    }
    cout << "Preamble (288 symbols): " << matches << "/288 matches" << endl;
    
    // Data extraction
    int data_start = preamble_start + 1440 * sps;
    cout << "\nData starts at sample " << data_start << endl;
    
    // Show first 80 data symbols
    cout << "\n--- First 80 data symbols (with RRC) ---" << endl;
    for (int i = 0; i < 80; i++) {
        int idx = data_start + i * sps;
        if (idx < (int)filtered.size()) {
            cout << decode_8psk_position(filtered[idx]);
            if ((i+1) % 20 == 0) cout << " ";
        }
    }
    cout << endl;
    
    // Compare with MSDMTDecoder output
    cout << "\n--- Compare with MSDMTDecoder ---" << endl;
    // (Run full decoder manually would be complex, so just show manual extraction)
    
    return 0;
}
