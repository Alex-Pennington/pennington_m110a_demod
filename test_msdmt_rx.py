import socket
import time
import threading

# Test MS-DMT RX with PhoenixNest TX file (cross-modem interop test)
# Key: After RXAUDIOINJECT, the modem needs ~1 second of silence to flush interleaver buffers
pcm = 'D:/pennington_m110a_demod/server/tx_pcm_out/20251209_173017_722.pcm'

print('1. Connecting data socket to port 4998 FIRST...')
data_sock = socket.socket()
data_sock.settimeout(1)
data_sock.connect(('localhost', 4998))
print('   Data socket connected!')

# Start reading thread immediately
all_rx = []
stop = [False]

def read_data():
    while not stop[0]:
        try:
            d = data_sock.recv(4096)
            if d:
                all_rx.append(d)
                print(f'DATA RECEIVED: {len(d)} bytes')
        except socket.timeout:
            pass
        except Exception as e:
            if not stop[0]:
                print(f'Data error: {e}')
            break

t = threading.Thread(target=read_data)
t.start()

print('2. Connecting control socket to port 4999...')
ctrl = socket.socket()
ctrl.settimeout(5)
ctrl.connect(('localhost', 4999))
print(f'   Ready: {ctrl.recv(1024).decode().strip()}')

print(f'3. Injecting: {pcm}')
ctrl.send(f'CMD:RXAUDIOINJECT:{pcm}\n'.encode())

# Wait for completion - interleaver buffers need time to flush
ctrl.settimeout(1)
for i in range(30):  # Wait up to 30 seconds
    try:
        msg = ctrl.recv(1024)
        if msg:
            print(f'CTRL: {msg.decode().strip()}')
            if b'COMPLETE' in msg:
                print('   Waiting for interleaver flush...')
                time.sleep(3)  # Extra time for buffer flush
                break
    except socket.timeout:
        pass

stop[0] = True
t.join(timeout=2)

total = b''.join(all_rx)
print(f'\nTotal decoded: {len(total)} bytes')
if total:
    try:
        print(f'Content (text): {total.decode("ascii", errors="replace")}')
    except:
        print(f'Content (raw): {repr(total[:100])}')
else:
    print('No data received on data port')

ctrl.close()
data_sock.close()
