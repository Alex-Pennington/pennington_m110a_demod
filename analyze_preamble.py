"""
Compare PCM waveforms from PhoenixNest TX and Brain TX
to identify differences
"""
import numpy as np
import struct
import sys

def read_pcm_file(path, sample_rate=48000):
    """Read 16-bit signed PCM file"""
    with open(path, 'rb') as f:
        data = f.read()
    samples = np.array(struct.unpack(f'<{len(data)//2}h', data), dtype=np.float32)
    samples = samples / 32768.0  # Normalize
    return samples, sample_rate

def analyze_preamble(samples, sample_rate=48000, carrier_freq=1800, baud_rate=2400):
    """Analyze the preamble structure"""
    samples_per_symbol = int(sample_rate / baud_rate)
    
    # Simple envelope detection
    analytic = np.abs(np.fft.ifft(np.fft.fft(samples) * 2 * (np.fft.fftfreq(len(samples)) > 0)))
    
    # Find signal start (where envelope exceeds threshold)
    threshold = 0.05 * np.max(analytic[:sample_rate])  # 5% of max in first second
    start_idx = np.argmax(analytic > threshold)
    
    print(f"Signal starts at sample {start_idx} ({start_idx/sample_rate*1000:.1f} ms)")
    print(f"Total samples: {len(samples)} ({len(samples)/sample_rate:.3f} sec)")
    
    # Demodulate first 2000 samples after start
    demod_len = min(4800, len(samples) - start_idx)  # 100ms at 48kHz
    chunk = samples[start_idx:start_idx + demod_len]
    
    # Create carrier
    t = np.arange(len(chunk)) / sample_rate
    carrier_i = np.cos(2 * np.pi * carrier_freq * t)
    carrier_q = -np.sin(2 * np.pi * carrier_freq * t)
    
    # Baseband I/Q
    bb_i = chunk * carrier_i
    bb_q = chunk * carrier_q
    
    # Low pass filter (simple moving average)
    window = samples_per_symbol
    bb_i_filt = np.convolve(bb_i, np.ones(window)/window, mode='valid')
    bb_q_filt = np.convolve(bb_q, np.ones(window)/window, mode='valid')
    
    # Sample at symbol centers
    num_symbols = len(bb_i_filt) // samples_per_symbol
    symbols_i = []
    symbols_q = []
    for i in range(num_symbols):
        idx = i * samples_per_symbol + samples_per_symbol // 2
        if idx < len(bb_i_filt):
            symbols_i.append(bb_i_filt[idx])
            symbols_q.append(bb_q_filt[idx])
    
    symbols = np.array(symbols_i) + 1j * np.array(symbols_q)
    
    # Map to 8-PSK symbols
    phases = np.angle(symbols) * 180 / np.pi
    psk8_symbols = np.round(phases / 45) % 8
    
    print(f"\nFirst 32 detected symbols (8-PSK indices):")
    print(psk8_symbols[:32].astype(int))
    
    return start_idx, psk8_symbols

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python analyze_preamble.py <pcm_file>")
        sys.exit(1)
    
    path = sys.argv[1]
    print(f"\n=== Analyzing: {path} ===\n")
    samples, sr = read_pcm_file(path)
    analyze_preamble(samples, sr)
