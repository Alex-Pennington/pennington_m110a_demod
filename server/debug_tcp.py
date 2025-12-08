#!/usr/bin/env python3
"""Debug TCP communication with M110A server"""
import socket
import time

HOST = '127.0.0.1'
CTRL_PORT = 4999
DATA_PORT = 4998

def recv_all(sock, timeout=0.5):
    """Receive all available data"""
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

def main():
    # Connect to both ports
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    ctrl.connect((HOST, CTRL_PORT))
    data.connect((HOST, DATA_PORT))
    
    print("Connected to server")
    
    # Read initial message
    init = recv_all(ctrl, 1.0)
    print(f"Initial: {init.decode()}")
    
    # Set mode to 600L
    ctrl.send(b"CMD:DATA RATE:600L\n")
    resp = recv_all(ctrl)
    print(f"Set mode: {resp.decode()}")
    
    # Enable recording
    ctrl.send(b"CMD:RECORD TX:ON\n")
    resp = recv_all(ctrl)
    print(f"Record on: {resp.decode()}")
    
    ctrl.send(b"CMD:RECORD PREFIX:test600L\n")
    resp = recv_all(ctrl)
    print(f"Record prefix: {resp.decode()}")
    
    # Send test data
    test_data = b"Hello World 600L Test!"
    data.send(test_data)
    print(f"Sent {len(test_data)} bytes on data port")
    
    # Trigger TX
    ctrl.send(b"CMD:SENDBUFFER\n")
    time.sleep(0.5)  # Wait for TX to start
    
    # Collect response for up to 15 seconds
    print("Waiting for SENDBUFFER response...")
    ctrl.settimeout(15.0)
    response = b''
    pcm_file = None
    start = time.time()
    while time.time() - start < 15:
        try:
            chunk = ctrl.recv(4096)
            if chunk:
                response += chunk
                print(f"  Received: {chunk.decode().strip()}")
                if b'FILE:' in chunk:
                    # Extract filename
                    text = chunk.decode()
                    idx = text.find('FILE:')
                    if idx >= 0:
                        pcm_file = text[idx+5:].strip().split('\n')[0]
                        print(f"  PCM file: {pcm_file}")
                if b'OK:SENDBUFFER' in response:
                    break
        except socket.timeout:
            break
    
    if not pcm_file:
        print("ERROR: No PCM file received!")
        return
    
    # Now inject the PCM for decode
    print(f"\nInjecting PCM: {pcm_file}")
    ctrl.send(f"CMD:RXAUDIOINJECT:{pcm_file}\n".encode())
    
    # Wait for COMPLETE - this is where the problem likely is
    print("Waiting for RXAUDIOINJECT response...")
    start = time.time()
    got_complete = False
    got_started = False
    rx_data = b''
    while time.time() - start < 20:  # 20 second timeout
        try:
            ctrl.settimeout(0.1)
            chunk = ctrl.recv(4096)
            if chunk:
                text = chunk.decode().strip()
                print(f"  [{time.time()-start:.1f}s] CTRL: {text}")
                if 'RXAUDIOINJECT:STARTED' in text:
                    got_started = True
                if 'RXAUDIOINJECT:COMPLETE' in text:
                    got_complete = True
                    break
        except socket.timeout:
            pass
        
        # Always check for data on data port
        try:
            data.settimeout(0.1)
            rx = data.recv(4096)
            if rx:
                rx_data += rx
                print(f"  [{time.time()-start:.1f}s] DATA: {len(rx)} bytes: {rx[:50]}")
        except socket.timeout:
            pass
    
    print(f"\nResult: started={got_started}, complete={got_complete}")
    print(f"Received {len(rx_data)} bytes of decoded data")
    if rx_data:
        print(f"Data: {rx_data}")
    
    ctrl.close()
    data.close()

if __name__ == '__main__':
    main()
