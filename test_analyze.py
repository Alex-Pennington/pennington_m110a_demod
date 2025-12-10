#!/usr/bin/env python3
"""Quick script to test CMD:ANALYZE on MS-DMT"""
import socket
import sys
import time

def analyze_pcm(path, host="localhost", port=4999):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    sock.settimeout(5.0)
    
    # Read MODEM READY
    data = sock.recv(1024)
    print(f"Initial: {data.decode().strip()}")
    
    # Send analyze command
    cmd = f"CMD:ANALYZE:{path}\n"
    sock.send(cmd.encode())
    print(f"Sent: {cmd.strip()}")
    
    # Read all response lines
    time.sleep(1)
    response = b""
    while True:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
            if not sock.recv(1, socket.MSG_PEEK | socket.MSG_DONTWAIT):
                break
        except:
            break
    
    print("Response:")
    print(response.decode())
    sock.close()

if __name__ == "__main__":
    # Reference PCM (MS-DMT generated)
    ref_path = r"D:\pennington_m110a_demod\refrence_pcm\tx_300L_20251206_202506_058.pcm"
    analyze_pcm(ref_path)
