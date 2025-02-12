import serial
import sys


def calculate_checksum(data):
    return sum(data[:8]) & 0xFF


def parse_payload(payload):
    print("\n")
    if len(payload) != 9:
        print("Invalid payload length")
        return

    distance = payload[2] | (payload[3] << 8)
    signal_strength = payload[4] | (payload[5] << 8)
    raw_temp = payload[6] | (payload[7] << 8)
    temp = (raw_temp / 8) - 256
    received_checksum = payload[8]
    calculated_checksum = calculate_checksum(payload)

    print(f"Distance: {distance} cm")
    print(f"Signal Strength: {signal_strength}")
    print(f"Raw Temp: {raw_temp}")
    print(f"Temp: {temp} C")
    print(
        f"Checksum: Received={received_checksum}, Calculated={calculated_checksum}, Status={'OK' if (received_checksum == calculated_checksum) else 'ERR'}"
    )


def read_serial(port):
    try:
        ser = serial.Serial(port, baudrate=115200)
        print(f"Listening on {port}...")

        while True:
            while ser.read(1) != b"\x59":
                continue

            data = b"\x59" + ser.read(8)

            if len(data) == 9 and data[0] == 0x59 and data[1] == 0x59:
                parse_payload(data)
            else:
                print("Invalid data or incorrect header")

    except serial.SerialException as e:
        print(f"Error opening serial port {e}")
    except KeyboardInterrupt:
        print("Exiting...")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Please pass in serial port")
        sys.exit(1)

    serial_port = sys.argv[1]
    read_serial(serial_port)
