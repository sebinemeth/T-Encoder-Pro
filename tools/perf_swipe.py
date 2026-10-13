#!/usr/bin/env python3
"""Trigger tile swaps through /tile and print the [perf] lines the firmware logs
(build with -D ENABLE_PERF_STATS=1). Usage: tools/perf_swipe.py <ip> <serial-port> [rounds]"""
import sys, time, threading, urllib.request, serial

ip, port = sys.argv[1], sys.argv[2]
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 3
ser = serial.Serial(port, 115200, timeout=0.2)
lines = []

def reader():
    while True:
        l = ser.readline().decode(errors="ignore").strip()
        if "[perf]" in l:
            lines.append((time.time(), l))
threading.Thread(target=reader, daemon=True).start()
time.sleep(2)
for r in range(rounds):
    for i in (1, 2, 3, 0):
        t = time.time()
        urllib.request.urlopen(f"http://{ip}/tile?i={i}", timeout=5).read()
        time.sleep(3)
        print(f"-- swap to tile {i}")
        for ts, l in lines:
            if ts >= t:
                print(l)
        lines.clear()
