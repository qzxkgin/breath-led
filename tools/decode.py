import argparse
import sys
import time

HEADER = 0xA5
PACKET_SIZE = 6
CRC_POLY = 0x1021


def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ CRC_POLY) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class Parser:
    def __init__(self):
        self.buf = bytearray()
        self.ok = 0
        self.bad = 0

    def feed(self, chunk):
        self.buf.extend(chunk)
        frames = []
        while len(self.buf) >= PACKET_SIZE:
            if self.buf[0] != HEADER:
                del self.buf[0]
                self.bad += 1
                continue
            packet = bytes(self.buf[:PACKET_SIZE])
            if crc16_ccitt(packet[:4]) != (packet[5] << 8 | packet[4]):
                del self.buf[0]
                self.bad += 1
                continue
            del self.buf[:PACKET_SIZE]
            self.ok += 1
            frames.append((packet[1], packet[2], packet[3]))
        return frames


def hex_tokens(line):
    for token in line.split():
        if len(token) == 2 and all(c in "0123456789abcdefABCDEF" for c in token):
            yield int(token, 16)


def show(r, g, b, parser, t):
    bar = "\x1b[48;2;%d;%d;%dm%s\x1b[0m" % (r, g, b, " " * 24)
    sys.stdout.write("\r  %6.2fs  rgb(%3d,%3d,%3d)  %s  ok=%d bad=%d" % (t, r, g, b, bar, parser.ok, parser.bad))
    sys.stdout.flush()


def run_text(args):
    parser = Parser()
    start = time.time()
    for line in sys.stdin:
        for rgb in parser.feed(bytes(hex_tokens(line))):
            show(rgb[0], rgb[1], rgb[2], parser, time.time() - start)
    return parser


def run_serial(args):
    try:
        import serial
    except ImportError:
        print("pyserial not installed: pip install pyserial", file=sys.stderr)
        return None
    port = serial.Serial(args.port, args.baud, timeout=0.2)
    parser = Parser()
    start = time.time()
    while time.time() - start < args.duration:
        for rgb in parser.feed(port.read(256)):
            show(rgb[0], rgb[1], rgb[2], parser, time.time() - start)
    port.close()
    return parser


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="read from a real serial port instead of stdin")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=5.0)
    args = ap.parse_args()

    parser = run_serial(args) if args.port else run_text(args)
    print()
    if parser is not None and parser.buf:
        print("trailing bytes in buffer: %d" % len(parser.buf))
    if parser is not None:
        print("ok=%d bad=%d" % (parser.ok, parser.bad))


if __name__ == "__main__":
    main()
