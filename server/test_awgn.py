#!/usr/bin/env python3
"""Test AWGN channel simulation"""
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

def test_awgn(snr_db):
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    ctrl.connect((HOST, CTRL_PORT))
    data_sock.connect((HOST, DATA_PORT))
    
    # Read initial
    recv_all(ctrl, 1.0)
    
    # Set mode
    ctrl.send(b"CMD:DATA RATE:2400S\n")
    resp = recv_all(ctrl)
    print(f"Set mode: {resp}")
    
    # Enable recording
    ctrl.send(b"CMD:RECORD TX:ON\n")
    recv_all(ctrl)
    
    # Send test data
    test_data = b"Test Message 12345"
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
    
    print(f"PCM file: {pcm_file}")
    
    # Set up channel with AWGN
    cmd = f"CMD:CHANNEL AWGN:{snr_db}\n"
    print(f"Sending: {cmd.strip()}")
    ctrl.send(cmd.encode())
    resp = recv_all(ctrl)
    print(f"Channel response: {resp}")
    
    # Inject PCM
    ctrl.send(f"CMD:RXAUDIOINJECT:{pcm_file}\n".encode())
    
    # Wait for decode
    rx_data = b''
    start = time.time()
    while time.time() - start < 20:
        try:
            ctrl.settimeout(0.1)
            chunk = ctrl.recv(4096)
            if chunk:
                print(f"CTRL: {chunk}")
                if b'COMPLETE' in chunk:
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
    
    # Check result
    print(f"\nSent: {test_data}")
    print(f"Received: {rx_data[:50]}... ({len(rx_data)} bytes)")
    
    if rx_data[:len(test_data)] == test_data:
        print("PASS")
        return True
    else:
        errors = sum(1 for a, b in zip(rx_data, test_data) if a != b)
        print(f"FAIL - {errors} byte errors in first {min(len(rx_data), len(test_data))} bytes")
        return False

if __name__ == '__main__':
    import sys
    snr = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    print(f"Testing 2400S at {snr} dB SNR")
    print("=" * 50)
    test_awgn(snr)
