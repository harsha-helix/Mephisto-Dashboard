import argparse
import sys

import serial
from serial.tools import list_ports


def parse_args():
    parser = argparse.ArgumentParser(
        description="Print nine processed ADC readings streamed by the Tiva board."
    )
    parser.add_argument(
        "port",
        nargs="?",
        help="Serial port, for example COM3 on Windows or /dev/ttyACM0 on Linux.",
    )
    parser.add_argument("--baud", type=int, default=115200)
    return parser.parse_args()


def print_available_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return

    print("Available serial ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")


def main():
    args = parse_args()

    if not args.port:
        print_available_ports()
        print("\nUsage: python read_adc.py COM3")
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            print(f"Reading ADC values from {args.port} at {args.baud} baud.")
            print("Press Ctrl+C to stop.\n")

            while True:
                line = ser.readline().decode("ascii", errors="replace").strip()
                if not line:
                    continue

                if line.startswith("/*") and line.endswith("*/"):
                    line = line[2:-2]

                parts = line.split(",")
                if len(parts) != 9:
                    print(f"Skipping malformed line: {line}")
                    continue

                try:
                    values = [int(part) for part in parts]
                except ValueError:
                    print(f"Skipping malformed line: {line}")
                    continue

                print(
                    "diode_limit={:4d}mA diode_actual={:4d}mA "
                    "diode_set={:3d}C diode_actual_temp={:3d}C "
                    "diode_tec_err={:4d} crystal_set={:3d}C "
                    "crystal_actual={:3d}C crystal_tec_err={:4d} "
                    "fault={:1d}".format(*values)
                )
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
