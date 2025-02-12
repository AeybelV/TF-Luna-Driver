import serial
import sys
from enum import Enum


COMMAND_HEADER = 0x5A


class CommandID(Enum):
    ID_GET_VERSION = 0x01
    ID_SOFT_RESET = 0x02
    ID_SAMPLE_FREQ = 0x03


# Custom exceptions


class TFLunaException(Exception):
    """Base class for TF-Luna Exceptions"""


class LunaInvalidHeader(Exception):
    """Rasied when response header is incorrect"""


class LunaInvalidChecksum(Exception):
    """Rasied when the checksum is incorrect"""


class LunaUnknownCommand(Exception):
    """Rasied when response contains a unknown command id"""


class LunaSerialTimeout(Exception):
    """Rasied when data is not received in expected timeframe"""


# Helper functions for handling commands


def calculate_checksum(data):
    return sum(data) & 0xFF


def build_command(command_id, parameters=[]):
    command_length = 4 + len(parameters)
    message = bytearray([COMMAND_HEADER, command_length, command_id.value] + parameters)
    checksum = calculate_checksum(message)
    message.append(checksum)

    return message


def read_exact(serial_dev, num_bytes):
    data = serial_dev.read(num_bytes)
    if len(data) != num_bytes:
        raise LunaSerialTimeout()
    return data


# Funcitons to send command and receive responses from the device


def send_command(serial_dev, command_id, parameters=[]):
    command = build_command(command_id, parameters)
    serial_dev.write(command)
    print(f"Sent: {command.hex()}")


def read_command_response(serial_dev):
    # Read the header byte
    header = read_exact(serial_dev, 1)
    if not header or header[0] != COMMAND_HEADER:
        raise LunaInvalidHeader(f"Invalid header byte: {header[0]:02X}")

    # Read response length
    length_byte = read_exact(serial_dev, 1)
    length = length_byte[0]

    # Get remaining bytes
    response = read_exact(serial_dev, length - 2 - 1)
    checksum = read_exact(serial_dev, 1)[0]

    # Extract the command id
    command_id = response[0]

    # Convert it into enum
    try:
        command_id = CommandID(command_id)
    except ValueError:
        raise LunaUnknownCommand("Unknown command ID: {command_id}")

    response_data = response[1:]

    # Verify checksum
    computed_checksum = calculate_checksum([COMMAND_HEADER, length] + list(response))

    if checksum != computed_checksum:
        raise LunaInvalidChecksum(
            f"Checksum mistmatch: expected {computed_checksum:02X}, received {checksum:02X}"
        )

    return (command_id, response_data)


def set_freq(serial_dev, freq):
    freq_lower = freq & 0xFF
    freq_upper = (freq >> 8) & 0xFF
    send_command(serial_dev, CommandID.ID_SAMPLE_FREQ, [freq_lower, freq_upper])
    command_id, response = read_command_response(serial_dev)
    if command_id != CommandID.ID_SAMPLE_FREQ:
        raise LunaUnknownCommand("Recevied a response for a different command")


def get_version(serial_dev):
    print("[SEND] Geting Version")
    send_command(serial_dev, CommandID.ID_GET_VERSION, [])
    command_id, response = read_command_response(serial_dev)
    if command_id != CommandID.ID_GET_VERSION:
        raise LunaUnknownCommand("Recevied a response for a different command")
    patch = response[0]
    minor = response[1]
    major = response[2]
    print(f"[RECEIVE] Version v{major}.{minor}.{patch}")


def setup_console(port):
    try:
        ser = serial.Serial(port, baudrate=115200)
        print(f"Listening on {port}...")

        # set trigger mode
        set_freq(ser, 0)

        # get version
        get_version(ser)

    except serial.SerialException as e:
        print(f"Error operating on serial port {e}")
    except KeyboardInterrupt:
        print("Exiting...")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Please pass in serial port")
        sys.exit(1)

    serial_port = sys.argv[1]
    setup_console(serial_port)
