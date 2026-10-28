import argparse
import math
import time

HEADER = 0xA5
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


def make_frame(r, g, b):
    body = bytes([HEADER, r, g, b])
    crc = crc16_ccitt(body)
    return body + bytes([crc & 0xFF, crc >> 8])


def main():
    parser = argparse.ArgumentParser(description="replay breathing frames on a serial port")
    parser.add_argument("port", help="COM3 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--color", default="0,160,255")
    parser.add_argument("--freq", type=float, default=0.5)
    parser.add_argument("--fps", type=float, default=50.0)
    parser.add_argument("--seconds", type=float, default=0.0)
    parser.add_argument("--dry-run", action="store_true", help="print frames instead of sending")
    args = parser.parse_args()

    base = [int(x) for x in args.color.split(",")]
    period = 1.0 / args.fps
    port = None
    if not args.dry_run:
        import serial
        port = serial.Serial(args.port, args.baud)

    print("crc('123456789') = 0x%04X" % crc16_ccitt(b"123456789"))

    start = time.time()
    next_at = start
    frames = 0
    while True:
        t = time.time() - start
        if args.seconds > 0 and t >= args.seconds:
            break
        env = 0.5 * (1.0 - math.cos(2.0 * math.pi * args.freq * t))
        frame = make_frame(round(base[0] * env), round(base[1] * env), round(base[2] * env))
        if port is not None:
            port.write(frame)
        elif frames < 3:
            print(" ".join("%02x" % b for b in frame))
        frames += 1
        next_at += period
        time.sleep(max(0.0, next_at - time.time()))

    if port is not None:
        port.write(make_frame(0, 0, 0))
        port.close()
    print("sent %d frames" % frames)


if __name__ == "__main__":
    main()
