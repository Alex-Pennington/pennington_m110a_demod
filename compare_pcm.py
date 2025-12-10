#!/usr/bin/env python3
"""Analyze and compare PCM files for interoperability debugging"""
import struct
import math
import sys
from pathlib import Path
import numpy as np

def analyze_pcm(path):
    """Analyze a 16-bit mono PCM file at 48kHz"""
    data = Path(path).read_bytes()
    num_samples = len(data) // 2
    samples = np.array(struct.unpack(f'<{num_samples}h', data), dtype=np.float32)
    
    duration = num_samples / 48000
    
    min_sample = int(np.min(samples))
    max_sample = int(np.max(samples))
    
    # DC offset (mean)
    dc_offset = np.mean(samples)
    
    # RMS level
    rms = np.sqrt(np.mean(samples**2))
    
    # Peak/RMS ratio (crest factor)
    peak = max(abs(min_sample), abs(max_sample))
    crest_factor = peak / rms if rms > 0 else 0
    
    # Zero crossings for frequency estimate
    zero_crossings = np.sum(np.diff(np.sign(samples)) != 0)
    est_freq_zc = (zero_crossings / 2) / duration if duration > 0 else 0
    
    # FFT for more accurate frequency estimate
    fft = np.fft.rfft(samples)
    freqs = np.fft.rfftfreq(len(samples), 1/48000)
    magnitude = np.abs(fft)
    
    # Find peak frequency (ignoring DC and low freq)
    # MIL-STD-188-110A uses 1800 Hz carrier, so look in 1000-3000 Hz range
    freq_mask = (freqs >= 1000) & (freqs <= 3000)
    masked_mag = np.where(freq_mask, magnitude, 0)
    peak_idx = np.argmax(masked_mag)
    peak_freq = freqs[peak_idx]
    
    # Also find overall peak (including low freq)
    overall_peak_idx = np.argmax(magnitude[10:]) + 10
    overall_peak_freq = freqs[overall_peak_idx]
    
    # Get top 5 frequencies
    top_indices = np.argsort(magnitude)[-10:][::-1]
    top_freqs = [(freqs[i], magnitude[i]) for i in top_indices if freqs[i] > 100]
    
    # Amplitude histogram
    abs_samples = np.abs(samples)
    silent = np.sum(abs_samples < 100)
    low = np.sum((abs_samples >= 100) & (abs_samples < 3000))
    med = np.sum((abs_samples >= 3000) & (abs_samples < 10000))
    high = np.sum(abs_samples >= 10000)
    
    return {
        'path': path,
        'size': len(data),
        'samples': num_samples,
        'duration': duration,
        'min_sample': min_sample,
        'max_sample': max_sample,
        'dc_offset': float(dc_offset),
        'rms': float(rms),
        'peak': peak,
        'crest_factor': float(crest_factor),
        'est_freq_zc': float(est_freq_zc),
        'peak_freq_fft': float(peak_freq),
        'overall_peak_freq': float(overall_peak_freq),
        'top_freqs': top_freqs[:5],
        'silent': int(silent),
        'low': int(low),
        'med': int(med),
        'high': int(high),
        'silent_pct': 100 * silent / num_samples,
        'low_pct': 100 * low / num_samples,
        'med_pct': 100 * med / num_samples,
        'high_pct': 100 * high / num_samples,
    }

def print_analysis(a):
    print(f"\n{'='*60}")
    print(f"File: {a['path']}")
    print(f"{'='*60}")
    print(f"  Size:        {a['size']:,} bytes ({a['samples']:,} samples)")
    print(f"  Duration:    {a['duration']:.3f} sec @ 48kHz")
    print(f"  Min/Max:     {a['min_sample']:+6d} / {a['max_sample']:+6d}")
    print(f"  DC Offset:   {a['dc_offset']:.1f}")
    print(f"  RMS Level:   {a['rms']:.1f}")
    print(f"  Peak:        {a['peak']}")
    print(f"  Crest:       {a['crest_factor']:.2f}")
    print(f"  Est Freq (ZC):  {a['est_freq_zc']:.0f} Hz")
    print(f"  Peak Freq 1-3kHz:{a['peak_freq_fft']:.0f} Hz  <-- Carrier band")
    print(f"  Overall Peak:   {a['overall_peak_freq']:.0f} Hz")
    print(f"  Top frequencies:")
    for freq, mag in a['top_freqs']:
        print(f"    {freq:7.0f} Hz  (mag: {mag:.0f})")
    print(f"  Histogram:")
    print(f"    Silent (<100):   {a['silent']:7,} ({a['silent_pct']:5.1f}%)")
    print(f"    Low (100-3k):    {a['low']:7,} ({a['low_pct']:5.1f}%)")
    print(f"    Med (3k-10k):    {a['med']:7,} ({a['med_pct']:5.1f}%)")
    print(f"    High (>10k):     {a['high']:7,} ({a['high_pct']:5.1f}%)")

def compare(a, b):
    print(f"\n{'='*60}")
    print("COMPARISON")
    print(f"{'='*60}")
    print(f"  RMS Ratio:     {a['rms'] / b['rms']:.2f}x" if b['rms'] > 0 else "  RMS Ratio:     N/A")
    print(f"  Peak Freq Diff:{abs(a['peak_freq_fft'] - b['peak_freq_fft']):.0f} Hz")
    print(f"  DC Diff:       {abs(a['dc_offset'] - b['dc_offset']):.1f}")
    
    # Check for potential issues
    issues = []
    if b['rms'] > 0:
        rms_ratio = a['rms'] / b['rms']
        if rms_ratio < 0.5 or rms_ratio > 2.0:
            issues.append(f"Large RMS difference ({rms_ratio:.2f}x) - amplitude normalization may be needed")
    
    freq_diff = abs(a['peak_freq_fft'] - b['peak_freq_fft'])
    if freq_diff > 50:
        issues.append(f"Peak frequency differs by {freq_diff:.0f} Hz - possible carrier/sample rate mismatch")
    
    if abs(a['dc_offset']) > 500 or abs(b['dc_offset']) > 500:
        issues.append("High DC offset detected - may cause correlation issues")
    
    if issues:
        print("\nPOTENTIAL ISSUES:")
        for issue in issues:
            print(f"  âš  {issue}")
    else:
        print("\n  âœ“ No obvious issues detected")

if __name__ == "__main__":
    # Reference PCM (Brain Modem generated, 300L mode)
    ref_path = r"D:\pennington_m110a_demod\refrence_pcm\tx_300L_20251206_202506_058.pcm"
    
    # Find most recent PhoenixNest TX output
    import os
    
    # Check both possible output directories
    tx_dirs = [
        r"D:\pennington_m110a_demod\tx_pcm_out",
        r"D:\pennington_m110a_demod\server\tx_pcm_out"
    ]
    
    # Get all PCM files from both directories
    all_pcm = []
    for tx_dir in tx_dirs:
        all_pcm.extend(Path(tx_dir).glob("*.pcm"))
    
    if not all_pcm:
        print("No PhoenixNest TX files found!")
        sys.exit(1)
    
    # Find most recent
    pcm_files = sorted(all_pcm, key=os.path.getmtime, reverse=True)
    pn_path = str(pcm_files[0])
    
    print("Analyzing PCM files for interoperability...")
    
    ref = analyze_pcm(ref_path)
    print_analysis(ref)
    
    pn = analyze_pcm(pn_path)
    print_analysis(pn)
    
    compare(ref, pn)
