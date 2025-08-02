
# Code generated with assistance from OpenAI's ChatGPT
# based on Gadgetbridge: https://codeberg.org/Freeyourgadget/Gadgetbridge/src/branch/master/app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/xiaomi/activity/impl/DailyDetailsParser.java

import struct
from typing import List, Dict
from datetime import datetime, timedelta, UTC
import matplotlib.pyplot as plt

class XiaomiComplexActivityParser:
    def __init__(self, header: bytes, buf: bytes):
        self.header = header
        self.buf = buf
        self.offset = 0
        self.current_group = -1
        self.current_val = 0
        self.current_group_bits = 0

    def reset(self):
        self.current_group = -1
        self.current_val = 0
        self.current_group_bits = 0

    def next_group(self, n_bits: int) -> bool:
        self.current_group += 1

        if self.current_group >= len(self.header) * 2:
            print(f"Header too small for group {self.current_group}")
            # Still advance offset just to stay in sync
            self._consume(n_bits)
            return False

        if not (self._get_current_nibble() & 0b1000):
            # Group not present
            return False

        self.current_group_bits = n_bits
        self.current_val = self._consume(n_bits)
        return True

    def has(self, idx: int) -> bool:
        nibble = self._get_nibble()
        return (nibble & (1 << (2 - idx))) != 0

    def get(self, idx: int, n_bits: int) -> int:
        shift = self.current_group_bits - n_bits - idx
        if shift < 0:
            raise ValueError(f"Negative shift! GroupBits={self.current_group_bits}, idx={idx}, nBits={n_bits}")
        mask = (1 << n_bits) - 1
        return (self.current_val >> shift) & mask

    def _get_current_nibble(self) -> int:
        header_byte_index = self.current_group // 2
        if header_byte_index >= len(self.header):
            raise IndexError("Header nibble index out of range")

        if self.current_group % 2 == 0:
            # High nibble
            return (self.header[header_byte_index] & 0xF0) >> 4
        else:
            # Low nibble
            return self.header[header_byte_index] & 0x0F

    def _get_nibble(self) -> int:
        byte_index = self.current_group // 2
        if self.current_group % 2 == 0:
            return (self.header[byte_index] >> 4) & 0xF
        else:
            return self.header[byte_index] & 0xF

    def _consume(self, n_bits: int) -> int:
        if n_bits == 8:
            if self.offset + 1 > len(self.buf):
                val = 0
                #raise IndexError("Tried to read past end of buffer (8 bits)")
            else:
                val = self.buf[self.offset]
                self.offset += 1
            return val
        elif n_bits == 16:
            if self.offset + 2 > len(self.buf):
                val = 0
                #raise IndexError("Tried to read past end of buffer (16 bits)")
            else:
                val = self.buf[self.offset] | (self.buf[self.offset + 1] << 8)
                self.offset += 2
            return val
        elif n_bits == 32:
            if self.offset + 4 > len(self.buf):
                val = 0
                #raise IndexError("Tried to read past end of buffer (32 bits)")
            else:
                val = (
                    self.buf[self.offset]
                    | (self.buf[self.offset + 1] << 8)
                    | (self.buf[self.offset + 2] << 16)
                    | (self.buf[self.offset + 3] << 24)
                )
                self.offset += 4
            return val
        else:
            raise ValueError(f"Unsupported bit width: {n_bits}")

def parse_xiaomi_v5_file(filename: str) -> List[Dict]:
    with open(filename, "rb") as f:
        raw = f.read()

    version = 5 # assumtion from file name

    index = 0
    file_id = raw[0:7]
    padding = raw[7]
    header = raw[8:15]  # 7 bytes
    data = raw[15:-4]   # skip CRC

    # Try to extract timestamp from file ID if possible
    # (this is guessed based on your earlier output)
    try:
        start_timestamp = struct.unpack('<I', file_id[0:4])[0]
        base_time = datetime.fromtimestamp(start_timestamp, UTC)
    except:
        base_time = datetime.utcnow()

    samples = []
    offset = 0
    minute = 0

    while offset < len(data):
        MAX_BYTES_PER_SAMPLE = 50
        sample_buf = data[offset:offset + MAX_BYTES_PER_SAMPLE]
        if len(sample_buf) < 1:
            break  # avoid passing empty or tiny buffer

        parser = XiaomiComplexActivityParser(header, sample_buf)

        parser.reset()
        sample = {"timestamp": (base_time + timedelta(minutes=minute)).isoformat()}
        ok = False
        includeExtraEntry = 0

        if parser.next_group(16):
            # TODO what's the first bit?

            if parser.has(1): # hasSecond
                includeExtraEntry = parser.get(1, 1);
            if parser.has(2): # hasThird
                sample["steps"] = parser.get(2, 14)
                ok = True

        if parser.next_group(8):
            # TODO activity type?

            if parser.has(1): # hasSecond
                sample["calories"] = parser.get(2, 6)

        if parser.next_group(8):
            pass # TODO

        if parser.next_group(16):
            pass # TODO distance

        if parser.next_group(8):
            if parser.has(0): # hasFirst
                # hr, 8 bits
                sample["heart_rate"] = parser.get(0, 8)
                ok = True

        if parser.next_group(8):
            if parser.has(0): # hasFirst
                pass # energy, 8 bits

        if parser.next_group(16):
            pass # TODO

        if version >= 3:
            if parser.next_group(8):
                if parser.has(0): # hasFirst
                    # spo2, 8 bits
                    sample["spo2"] = parser.get(0, 8)
                    ok = True

            if parser.next_group(8):
                if parser.has(0): # hasFirst
                    # stress, 8 bits
                    stress = parser.get(0, 8)
                    if stress != 255:
                        sample["stress"] = stress
                        ok = True

        if includeExtraEntry == 1:
            if parser.next_group(8):
                pass # TODO

        if version >= 4:
            parser.next_group(16) # TODO: light value (short)
            parser.next_group(16) # TODO: body momentum (short)

        consumed = parser.offset
        offset += consumed
        if ok:
            samples.append(sample)
        minute += 1

    return samples


if __name__ == "__main__":
    samples = parse_xiaomi_v5_file("xiaomi_20250719T143946_01_16_00_v5.bin")
    #for s in samples:
    #    print(s)

    #g

    # Extract timestamps and values for each metric
    times = [datetime.fromisoformat(s["timestamp"]) for s in samples]

    # Collect data series, handle missing keys
    steps = [s.get("steps", None) for s in samples]
    heart_rate = [s.get("heart_rate", None) for s in samples]
    calories = [s.get("calories", None) for s in samples]
    spo2 = [s.get("spo2", None) for s in samples]
    stress = [s.get("stress", None) for s in samples]

    # Create a plot with multiple subplots
    fig, axs = plt.subplots(5, 1, figsize=(12, 15), sharex=True)

    axs[0].plot(times, steps, marker='o', label="Steps")
    axs[0].set_ylabel("Steps")
    axs[0].legend()

    axs[1].plot(times, heart_rate, marker='o', color='r', label="Heart Rate")
    axs[1].set_ylabel("Heart Rate (bpm)")
    axs[1].legend()

    axs[2].plot(times, calories, marker='o', color='orange', label="Calories")
    axs[2].set_ylabel("Calories")
    axs[2].legend()

    axs[3].plot(times, spo2, marker='o', color='green', label="SpO2")
    axs[3].set_ylabel("SpO2 (%)")
    axs[3].legend()

    axs[4].plot(times, stress, marker='o', color='purple', label="Stress")
    axs[4].set_ylabel("Stress")
    axs[4].legend()

    axs[4].set_xlabel("Time")

    plt.suptitle("Xiaomi Activity Data Over Time")
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.show()
