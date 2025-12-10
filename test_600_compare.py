#!/usr/bin/env python3
"""Quick test to compare 600S and 600L data extraction."""

import socket
import time
import os

REF_PCM_DIR = r"D:\pennington_m110a_demod\refrence_pcm"

def test_mode(mode, pcm_file):
    """Test a single mode."""
    pcm_path = os.path.join(REF_PCM_DIR, pcm_file)
    
    print(f"\n{'='*60}")
    print(f"Testing {mode}")
    print(f"{'='*60}")
    
    # Connect to control and data ports
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ctrl.connect(('127.0.0.1', 4999))
    ctrl.settimeout(5)
    
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock.connect(('127.0.0.1', 4998))
    data_sock.settimeout(2)
    
    # Read MODEM READY
    ctrl.recv(1024)
    
    # Set mode
    ctrl.sendall(f"CMD:DATA RATE:{mode}\n".encode())
    time.sleep(0.2)
    ctrl.recv(1024)
    
    # Inject reference PCM
    ctrl.sendall(f"CMD:RXAUDIOINJECT:{pcm_path}\n".encode())
    
    # Wait for response
    time.sleep(10)  # Give it time to process
    
    # Read decoded data
    chunks = []
    try:
        while True:
            chunk = data_sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    except socket.timeout:
        pass
    
    decoded = b''.join(chunks).decode(errors='replace') if chunks else ""
    
    print(f"Decoded ({len(decoded)} bytes): {repr(decoded[:80])}")
    
    ctrl.close()
    data_sock.close()
    
    return decoded

def main():
    print("600S vs 600L Comparison Test")
    print("Check server console for debug output")
    
    # Test 600S (working)
    result_600s = test_mode("600S", "tx_600S_20251206_202518_709.pcm")
    
    time.sleep(2)
    
    # Test 600L (partial)
    result_600l = test_mode("600L", "tx_600L_20251206_202521_953.pcm")
    
    print("\n" + "="*60)
    print("COMPARISON")
    print("="*60)
    print(f"600S starts with: {repr(result_600s[:30])}")
    print(f"600L starts with: {repr(result_600l[:30])}")

if __name__ == "__main__":
    main()
