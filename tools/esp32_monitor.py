import serial
import time
import threading
import sys

buf = bytearray()
stop = [False]

def reader(s):
    while not stop[0]:
        try:
            d = s.read(256)
            if d:
                buf.extend(d)
        except Exception as e:
            print(f"Read error: {e}", file=sys.stderr)
            break

try:
    s = serial.Serial('COM9', 115200, timeout=0.05)
    print("Port opened OK", file=sys.stderr)
except Exception as e:
    print(f"Cannot open COM9: {e}", file=sys.stderr)
    sys.exit(1)

t = threading.Thread(target=reader, args=(s,), daemon=True)
t.start()

time.sleep(0.1)
s.setRTS(True)
time.sleep(0.05)
s.setRTS(False)
print("Reset done, waiting...", file=sys.stderr)
time.sleep(7)
stop[0] = True
t.join()
s.close()

print(f"Captured {len(buf)} bytes", file=sys.stderr)
print(buf.decode('utf-8', errors='replace'))
