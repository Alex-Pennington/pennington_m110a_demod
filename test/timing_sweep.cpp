/**
 * Sweep timing offset to find best alignment
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <iomanip>
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

vector<float> make_rrc(int ntaps, float alpha, int sps) {
    vector<float> h(ntaps);
    for (int i = 0; i < ntaps; i++) {
        float t = (i - (ntaps-1)/2.0f) / sps;
        if (fabs(t) < 1e-6) {
            h[i] = 1.0f - alpha + 4.0f * alpha / M_PI;
        } else if (fabs(fabs(t) - 1.0f/(4.0f*alpha)) < 1e-6) {
            h[i] = alpha/sqrt(2.0f) * ((1+2.0f/M_PI)*sin(M_PI/(4*alpha)) + 
                                        (1-2.0f/M_PI)*cos(M_PI/(4*alpha)));
        } else {
            float num = sin(M_PI*t*(1-alpha)) + 4*alpha*t*cos(M_PI*t*(1+alpha));
            float den = M_PI*t*(1 - pow(4*alpha*t, 2));
            h[i] = num / den;
        }
    }
    float sum = 0;
    for (auto v : h) sum += v;
    for (auto& v : h) v /= sum;
    return h;
}

int main() {
    string filename = "/home/claude/m110a_demod/ref_pcm/tx_2400S_20251206_202547_345.pcm";
    
    auto samples = read_pcm(filename);
    
    // Downconvert and filter
    int sps = 20;
    auto rrc = make_rrc(6 * sps + 1, 0.35f, sps);
    int half_rrc = rrc.size() / 2;
    
    float fc = 1800.0f, fs = 48000.0f;
    float phase_inc = 2 * M_PI * fc / fs;
    
    vector<complex<float>> filtered(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        float phase = i * phase_inc;
        complex<float> bb(samples[i] * cos(phase), -samples[i] * sin(phase));
        
        // Apply RRC
        complex<float> sum(0, 0);
        for (size_t j = 0; j < rrc.size(); j++) {
            if (i >= j) {
                float ph = (i - j) * phase_inc;
                complex<float> s(samples[i-j] * cos(ph), -samples[i-j] * sin(ph));
                sum += s * rrc[j];
            }
        }
        filtered[i] = sum;
    }
    
    // Expected D2 pattern (symbols 448-479)
    string expected;
    for (int i = 448; i < 480; i++) {
        uint8_t base = msdmt::psymbol[4][i % 8];
        uint8_t scr = msdmt::pscramble[i % 32];
        expected += '0' + (base + scr) % 8;
    }
    
    int preamble_start = 257;  // Detected earlier
    
    cout << "=== Timing Sweep ===" << endl;
    cout << "Expected D2: " << expected << endl;
    
    // Try different timing offsets
    for (int offset = -10; offset <= 10; offset++) {
        string actual;
        int matches = 0;
        
        for (int i = 448; i < 480; i++) {
            int idx = preamble_start + i * sps + offset;
            if (idx >= 0 && idx < (int)filtered.size()) {
                int pos = decode_8psk_position(filtered[idx]);
                actual += '0' + pos;
                if (actual.back() == expected[i-448]) matches++;
            }
        }
        
        cout << "Offset " << setw(3) << offset << ": " << actual 
             << " matches=" << matches << "/32" << endl;
    }
    
    return 0;
}
