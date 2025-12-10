#!/usr/bin/env python3
"""Test PhoenixNest RX with reference PCM files."""

import socket
import time
import os

REF_PCM_DIR = r"D:\pennington_m110a_demod\refrence_pcm"

# Reference PCM files and their expected content
REF_FILES = {
    "75S": "tx_75S_20251206_202410_888.pcm",
    "75L": "tx_75L_20251206_202421_539.pcm",
    "150S": "tx_150S_20251206_202440_580.pcm",
    "150L": "tx_150L_20251206_202446_986.pcm",
    "300S": "tx_300S_20251206_202501_840.pcm",
    "300L": "tx_300L_20251206_202506_058.pcm",
    "600S": "tx_600S_20251206_202518_709.pcm",
    "600L": "tx_600L_20251206_202521_953.pcm",
    "1200S": "tx_1200S_20251206_202533_636.pcm",
    "1200L": "tx_1200L_20251206_202536_295.pcm",
    "2400S": "tx_2400S_20251206_202547_345.pcm",
    "2400L": "tx_2400L_20251206_202549_783.pcm",
}

EXPECTED_MSG = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"

def send_cmd(sock, cmd):
    """Send command and return response."""
    sock.sendall((cmd + '\n').encode())
    time.sleep(0.1)
    response = b''
    sock.settimeout(1.0)
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
            if b'\n' in chunk:
                break
    except socket.timeout:
        pass
    return response.decode().strip()

def recv_until(sock, pattern, timeout=30):
    """Receive until pattern found or timeout."""
    sock.settimeout(1.0)
    all_data = ""
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = sock.recv(4096).decode()
            all_data += chunk
            print(f"  <<< {chunk.strip()}")
            if pattern in all_data:
                return all_data
        except socket.timeout:
            continue
    return all_data

def test_mode(mode):
    """Test a single mode with reference PCM."""
    pcm_file = REF_FILES.get(mode)
    if not pcm_file:
        print(f"Unknown mode: {mode}")
        return False
    
    pcm_path = os.path.join(REF_PCM_DIR, pcm_file)
    if not os.path.exists(pcm_path):
        print(f"PCM file not found: {pcm_path}")
        return False
    
    print(f"\n{'='*60}")
    print(f"Testing {mode} with {pcm_file}")
    print(f"{'='*60}")
    
    # Connect to control port (MS-DMT compatible port 4999)
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ctrl.connect(('127.0.0.1', 4999))
    ctrl.settimeout(5)
    
    # Read MODEM READY
    ready = ctrl.recv(1024).decode().strip()
    print(f"<<< {ready}")
    
    # Connect to data port BEFORE inject (so we don't miss data)
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock.connect(('127.0.0.1', 4998))
    data_sock.settimeout(1)
    
    # Set mode
    print(f">>> CMD:DATA RATE:{mode}")
    resp = send_cmd(ctrl, f"CMD:DATA RATE:{mode}")
    print(f"<<< {resp}")
    
    # Inject reference PCM
    print(f">>> CMD:RXAUDIOINJECT:{pcm_path}")
    ctrl.sendall(f"CMD:RXAUDIOINJECT:{pcm_path}\n".encode())
    
    # Wait for response and DCD
    all_responses = recv_until(ctrl, "RXAUDIOINJECT:COMPLETE", timeout=45)
    
    # Check results
    # Check for mode detection (not NO DCD as first STATUS:RX message)
    got_dcd = False
    if "STATUS:RX:" in all_responses:
        # Get the first STATUS:RX line
        lines = all_responses.split('\n')
        for line in lines:
            if "STATUS:RX:" in line:
                if "NO DCD" not in line:
                    got_dcd = True
                    print(f"  Mode detected: {line}")
                break
    
    got_complete = "RXAUDIOINJECT:COMPLETE" in all_responses
    
    # Read decoded data from data port (already connected before inject)
    decoded = ""
    try:
        # Read whatever data is available
        chunks = []
        data_sock.settimeout(2)
        try:
            while True:
                chunk = data_sock.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
        except socket.timeout:
            pass
        
        if chunks:
            decoded = b''.join(chunks).decode(errors='replace')
            print(f"  Decoded data ({len(decoded)} bytes): {repr(decoded[:100])}")
        else:
            print(f"  No data received on data port")
            
        data_sock.close()
    except Exception as e:
        print(f"  Error reading data: {e}")
    
    ctrl.close()
    
    # Evaluate - check for expected message content
    has_expected_content = "QUICK BROWN FOX" in decoded or "THE QUICK BROWN FOX" in decoded
    success = len(decoded) > 0 and has_expected_content
    print(f"\nResult: {'PASS' if success else 'FAIL'}")
    print(f"  DCD: {got_dcd}, Complete: {got_complete}, Decoded bytes: {len(decoded)}")
    print(f"  Contains expected message: {has_expected_content}")
    
    return success

def main():
    print("PhoenixNest Reference PCM Decode Test")
    print("="*60)
    
    # Test all modes
    results = {}
    for mode in REF_FILES.keys():
        success = test_mode(mode)
        results[mode] = success
        time.sleep(1)
    
    # Summary
    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)
    passed = sum(1 for v in results.values() if v)
    total = len(results)
    print(f"\nPassed: {passed}/{total}")
    for mode, success in results.items():
        print(f"  {mode}: {'PASS' if success else 'FAIL'}")
    
    return passed == total

if __name__ == "__main__":
    main()