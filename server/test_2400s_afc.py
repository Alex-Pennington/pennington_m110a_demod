#!/usr/bin/env python3
"""Debug 2400S AFC issue - why 1Hz fails but 2Hz passes"""
import socket
import time

HOST = '127.0.0.1'
CTRL_PORT = 4999
DATA_PORT = 4998

def recv_all(sock, timeout=0.5):
    sock.settimeout(timeout)
    data = b''
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data

def test_with_verbose(mode, freq_hz):
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    ctrl.connect((HOST, CTRL_PORT))
    data_sock.connect((HOST, DATA_PORT))
    
    # Read initial
    initial = recv_all(ctrl, 1.0)
    print(f"Initial: {initial[:200]}")
    
    # Set mode
    ctrl.send(f"CMD:DATA RATE:{mode}\n".encode())
    resp = recv_all(ctrl)
    print(f"Set mode: {resp}")
    
    # Enable recording
    ctrl.send(b"CMD:RECORD TX:ON\n")
    recv_all(ctrl)
    ctrl.send(f"CMD:RECORD PREFIX:debug_2400s\n".encode())
    recv_all(ctrl)
    
    # Send test data
    test_data = b"Test123"  # Short message
    data_sock.send(test_data)
    print(f"Sent: {test_data}")
    
    # Trigger TX
    ctrl.send(b"CMD:SENDBUFFER\n")
    
    # Wait for PCM file
    pcm_file = None
    ctrl.settimeout(15.0)
    response = b''
    start = time.time()
    while time.time() - start < 15:
        try:
            chunk = ctrl.recv(4096)
            if chunk:
                response += chunk
                if b'FILE:' in chunk:
                    text = chunk.decode()
                    idx = text.find('FILE:')
                    if idx >= 0:
                        pcm_file = text[idx+5:].strip().split('\n')[0]
                        print(f"PCM file: {pcm_file}")
                if b'OK:SENDBUFFER' in response:
                    break
        except socket.timeout:
            break
    
    if not pcm_file:
        print("ERROR - No PCM file")
        ctrl.close()
        data_sock.close()
        return
    
    # Set up channel with frequency offset
    ctrl.send(f"CMD:CHANNEL FREQOFFSET:{freq_hz}\n".encode())
    resp = recv_all(ctrl)
    print(f"Channel setup: {resp}")
    
    # Inject PCM
    ctrl.send(f"CMD:RXAUDIOINJECT:{pcm_file}\n".encode())
    
    # Wait for decode with verbose capture
    rx_data = b''
    ctrl_responses = b''
    got_complete = False
    start = time.time()
    
    while time.time() - start < 25:
        try:
            ctrl.settimeout(0.1)
            chunk = ctrl.recv(4096)
            if chunk:
                ctrl_responses += chunk
                print(f"CTRL: {chunk}")
                if b'COMPLETE' in chunk:
                    got_complete = True
                    break
        except socket.timeout:
            pass
        
        try:
            data_sock.settimeout(0.1)
            rx = data_sock.recv(4096)
            if rx:
                rx_data += rx
                print(f"DATA: {rx[:50]}... ({len(rx)} bytes)")
        except socket.timeout:
            pass
    
    # Clean up
    ctrl.send(b"CMD:CHANNEL OFF\n")
    recv_all(ctrl)
    ctrl.close()
    data_sock.close()
    
    print(f"\n=== RESULT ===")
    print(f"Sent: {test_data} ({len(test_data)} bytes)")
    print(f"Received: {rx_data[:50]}... ({len(rx_data)} bytes)")
    print(f"Full received (hex): {rx_data[:80].hex()}")
    # Check if first N bytes match
    if rx_data[:len(test_data)] == test_data:
        # Check if remaining are zeros
        non_zero = sum(1 for b in rx_data[len(test_data):] if b != 0)
        if non_zero == 0:
            print(f"PASS! (Data correct, {len(rx_data) - len(test_data)} trailing zeros)")
        else:
            print(f"PARTIAL - Data correct but {non_zero} non-zero bytes after")
    elif rx_data == test_data:
        print("PASS!")
    else:
        errors = sum(1 for a, b in zip(rx_data, test_data) if a != b)
        errors += abs(len(rx_data) - len(test_data))
        print(f"FAIL - {errors} byte differences")
        # Show mismatches
        for i, (a, b) in enumerate(zip(rx_data[:20], test_data)):
            if a != b:
                print(f"  Byte {i}: got {hex(a)}, expected {hex(b)}")

if __name__ == '__main__':
    import sys
    mode = sys.argv[1] if len(sys.argv) > 1 else "2400S"
    freq = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    print(f"Testing {mode} @ {freq}Hz")
    print("=" * 50)
    test_with_verbose(mode, freq)
