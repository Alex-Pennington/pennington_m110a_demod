/**
 * @file sdr_frontend.h
 * @brief SDR I/Q -> 48 kHz USB audio, the front end of the I/Q receive path.
 *
 * Input is complex baseband from an SDR at any rate IQSource supports
 * (e.g. an RSP2 at 2 MS/s) with the USB suppressed carrier ("dial") at
 * +shift_hz. The chain:
 *
 *   1. NCO: move the dial from +shift_hz to 0 Hz
 *   2. IQSource: decimate to 48 kHz complex
 *   3. USB filter: complex FIR passing +300..+3300 Hz (rejects the LSB side)
 *   4. real part: audio with the 110A carrier at 1800 Hz, as ModemRX expects
 *
 * Output audio is unnormalised: an input tone of amplitude A (full scale 1.0)
 * at +shift_hz + f, 300 < f < 3300, comes out as a cosine of amplitude A at f.
 * The caller sets the level ModemRX needs (see m110a_rx).
 *
 * Streaming: push() any number of input samples at a time; the output is the
 * same as pushing everything at once.
 */

#ifndef M110A_API_SDR_FRONTEND_H
#define M110A_API_SDR_FRONTEND_H

#include "iq_source.h"
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace m110a {

class SdrFrontEnd {
public:
    static constexpr double AUDIO_RATE = 48000.0;
    static constexpr double USB_LOW_HZ = 300.0;
    static constexpr double USB_HIGH_HZ = 3300.0;
    static constexpr int USB_TAPS = 255;

    /**
     * @param input_rate_hz SDR sample rate
     * @param shift_hz      frequency of the dial in the input (dial - tuner,
     *                      corrected for the receiver's clock error)
     */
    SdrFrontEnd(double input_rate_hz, double shift_hz)
        : input_rate_(input_rate_hz)
        , iq_(input_rate_hz, IQSource::Format::FLOAT32_INTERLEAVED, AUDIO_RATE)
        , rot_(1.0, 0.0)
        , step_(std::polar(1.0, -2.0 * M_PI * shift_hz / input_rate_hz))
        , hist_(USB_TAPS, {0.0f, 0.0f}) {
        // USB band-pass: Hamming-windowed lowpass of half-width 1500 Hz,
        // shifted up to the band centre 1800 Hz. Unity gain at the centre.
        const double fc = (USB_HIGH_HZ - USB_LOW_HZ) / 2.0;
        const double f0 = (USB_HIGH_HZ + USB_LOW_HZ) / 2.0;
        taps_.resize(USB_TAPS);
        const int M = USB_TAPS - 1;
        double sum = 0.0;
        std::vector<double> lp(USB_TAPS);
        for (int k = 0; k < USB_TAPS; k++) {
            double t = k - M / 2.0;
            double h = (t == 0.0) ? 2.0 * fc / AUDIO_RATE
                                  : std::sin(2.0 * M_PI * fc / AUDIO_RATE * t) / (M_PI * t);
            lp[k] = h * (0.54 - 0.46 * std::cos(2.0 * M_PI * k / M));
            sum += lp[k];
        }
        for (int k = 0; k < USB_TAPS; k++) {
            double t = k - M / 2.0;
            taps_[k] = std::complex<float>(std::polar(lp[k] / sum, 2.0 * M_PI * f0 / AUDIO_RATE * t));
        }
    }

    double input_rate() const { return input_rate_; }

    /// Push complex input samples; appends audio to out.
    void push(const std::complex<float>* x, size_t n, std::vector<float>& out) {
        shifted_.resize(n);
        for (size_t i = 0; i < n; i++) {
            shifted_[i] = std::complex<float>(std::complex<double>(x[i]) * rot_);
            rot_ *= step_;
            if ((++nco_count_ & 1023) == 0) rot_ /= std::abs(rot_);  // hold |rot| = 1
        }
        iq_.push_samples_complex(shifted_.data(), n);
        bb_.resize(iq_.samples_available());
        size_t got = iq_.read(bb_.data(), bb_.size());
        for (size_t i = 0; i < got; i++) out.push_back(usb(bb_[i]));
    }

    /// Push interleaved int16 I/Q (cs16, full scale 32768).
    void push_cs16(const int16_t* iq, size_t pairs, std::vector<float>& out) {
        conv_.resize(pairs);
        for (size_t i = 0; i < pairs; i++)
            conv_[i] = std::complex<float>(iq[2 * i] / 32768.0f, iq[2 * i + 1] / 32768.0f);
        push(conv_.data(), pairs, out);
    }

    /// Push interleaved float32 I/Q (cf32).
    void push_cf32(const float* iq, size_t pairs, std::vector<float>& out) {
        push(reinterpret_cast<const std::complex<float>*>(iq), pairs, out);
    }

private:
    float usb(std::complex<float> v) {
        hist_[pos_] = v;
        // y[n] = sum_k h[k] x[n-k]; only the real part is wanted
        float acc = 0.0f;
        size_t j = pos_;
        for (int k = 0; k < USB_TAPS; k++) {
            acc += taps_[k].real() * hist_[j].real() - taps_[k].imag() * hist_[j].imag();
            j = (j == 0) ? USB_TAPS - 1 : j - 1;
        }
        pos_ = (pos_ + 1) % USB_TAPS;
        return acc;
    }

    double input_rate_;
    IQSource iq_;
    std::complex<double> rot_, step_;
    uint64_t nco_count_ = 0;
    std::vector<std::complex<float>> taps_;
    std::vector<std::complex<float>> hist_;
    size_t pos_ = 0;
    std::vector<std::complex<float>> shifted_, bb_, conv_;
};

}  // namespace m110a

#endif  // M110A_API_SDR_FRONTEND_H
