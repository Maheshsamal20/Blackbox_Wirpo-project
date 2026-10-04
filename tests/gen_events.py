#!/usr/bin/env python3
"""Write binary bb_event records (128 bytes each) to stdout.

usage: gen_events.py START_SEQ COUNT LEVEL PREFIX
Layout matches include/bb_uapi.h: u64 ts, u64 seq, u32 pid, u8 level, u8 source, u16 len, char msg[104]
"""
import struct
import sys
import time

start, count, level, prefix = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
out = sys.stdout.buffer
for i in range(count):
    msg = f"{prefix} {start + i}".encode()[:103]
    out.write(struct.pack("<QQIBBH104s", time.time_ns(), start + i, 4242, level, 0, len(msg), msg))
out.flush()
