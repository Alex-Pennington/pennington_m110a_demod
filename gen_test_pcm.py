#!/usr/bin/env python3
"""Generate a test PCM file using PhoenixNest for amplitude comparison."""

import socket
import time
import os

def send_cmd(sock, cmd):
    """Send command and get response."""
    sock.sendall((cmd + '\n').encode())
    response = b''
    while True:
        chunk = sock.recv(1024)
        response += chunk
        if b'\n' in chunk:
            break
    return response.decode().strip()

def main():
    # Connect to control port
    ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ctrl.connect(('127.0.0.1', 5100))
    ctrl.settimeout(30)
    
    # Read MODEM READY
    ready = ctrl.recv(1024).decode().strip()
    print(f"<<< {ready}")
    
    # Set mode
    print(">>> CMD:DATA RATE:300L")
    resp = send_cmd(ctrl, "CMD:DATA RATE:300L")
    print(f"<<< {resp}")
    
    # Enable TX recording
    print(">>> CMD:RECORD TX:ON")
    resp = send_cmd(ctrl, "CMD:RECORD TX:ON")
    print(f"<<< {resp}")
    
    # Connect to data port and send data
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock.connect(('127.0.0.1', 5101))
    test_data = b"HELLO WORLD TEST MESSAGE FOR AMPLITUDE COMPARISON 12345 ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    data_sock.sendall(test_data)
    data_sock.close()
    print(f"Sent {len(test_data)} bytes to data port")
    
    # Trigger transmit
    print(">>> CMD:SENDBUFFER")
    ctrl.sendall(b"CMD:SENDBUFFER\n")
    
    # Wait for TX complete with timeout
    ctrl.settimeout(60)
    print("Waiting for TX completion...")
    
    while True:
        try:
            response = ctrl.recv(1024).decode().strip()
            for line in response.split('\n'):
                print(f"<<< {line}")
                if 'TX:IDLE' in line:
                    print("TX Complete!")
                    ctrl.close()
                    return
        except socket.timeout:
            print("Timeout waiting for response")
            break
    
    ctrl.close()

if __name__ == "__main__":
    main()
