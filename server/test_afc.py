#!/usr/bin/env python3
"""Test AFC with specific frequency offset"""
import socket
import time
import struct

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

def test_freq_offset(mode, freq_hz):
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    ctrl.connect((HOST, CTRL_PORT))
    data_sock.connect((HOST, DATA_PORT))
    
    # Read initial
    recv_all(ctrl, 1.0)
    
    # Set mode
    ctrl.send(f"CMD:DATA RATE:{mode}\n".encode())
    recv_all(ctrl)
    
    # Enable recording
    ctrl.send(b"CMD:RECORD TX:ON\n")
    recv_all(ctrl)
    ctrl.send(f"CMD:RECORD PREFIX:afc_test_{freq_hz}hz\n".encode())
    recv_all(ctrl)
    
    # Send test data
    test_data = b"AFC Test Message 12345"
    data_sock.send(test_data)
    
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
                if b'OK:SENDBUFFER' in response:
                    break
        except socket.timeout:
            break
    
    if not pcm_file:
        print(f"  {mode} @ {freq_hz}Hz: ERROR - No PCM file")
        ctrl.close()
        data_sock.close()
        return False
    
    # Set up channel with frequency offset
    ctrl.send(f"CMD:CHANNEL FREQOFFSET:{freq_hz}\n".encode())
    recv_all(ctrl)
    
    # Inject PCM
    ctrl.send(f"CMD:RXAUDIOINJECT:{pcm_file}\n".encode())
    
    # Wait for decode
    rx_data = b''
    got_complete = False
    start = time.time()
    while time.time() - start < 20:
        try:
            ctrl.settimeout(0.1)
            chunk = ctrl.recv(4096)
            if chunk:
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
        except socket.timeout:
            pass
    
    # Clean up
    ctrl.send(b"CMD:CHANNEL OFF\n")
    recv_all(ctrl)
    ctrl.close()
    data_sock.close()
    
    # Check result - data should start with test_data, rest should be zeros or minimal garbage
    if rx_data == test_data:
        print(f"  {mode} @ {freq_hz}Hz: PASS - Received {len(rx_data)} bytes correctly")
        return True
    elif rx_data and rx_data[:len(test_data)] == test_data:
        # Data is correct but has trailing bytes (zeros or padding)
        trailing = rx_data[len(test_data):]
        non_zero = sum(1 for b in trailing if b != 0)
        if non_zero <= 5:  # Allow up to 5 non-zero trailing bytes (decoding noise)
            print(f"  {mode} @ {freq_hz}Hz: PASS - Received {len(test_data)} bytes correctly (+{len(trailing)} trailing)")
            return True
        else:
            print(f"  {mode} @ {freq_hz}Hz: FAIL - Data correct but {non_zero} garbage bytes after")
            return False
    elif rx_data:
        # Calculate BER for first N bytes
        errors = sum(1 for a, b in zip(rx_data, test_data) if a != b)
        errors += max(0, len(test_data) - len(rx_data))  # Missing bytes are errors
        ber = errors / max(len(test_data), 1)
        print(f"  {mode} @ {freq_hz}Hz: FAIL - BER={ber:.1%} (got {len(rx_data)} bytes, first bytes: {rx_data[:20].hex()})")
        return False
    else:
        print(f"  {mode} @ {freq_hz}Hz: FAIL - No data received")
        return False

def main():
    print("AFC Test - Testing frequency offset handling")
    print("=" * 50)
    
    modes = ["600S", "1200S", "2400S"]
    freq_offsets = [0, 0.5, 1, 2, 5, 10]
    
    for mode in modes:
        print(f"\nMode: {mode}")
        for freq in freq_offsets:
            test_freq_offset(mode, freq)

if __name__ == '__main__':
    main()
