#!/usr/bin/env python3
"""
Python client for forwarding input events to a remote server.
Converts key press/release events from a Linux input device to network messages.
"""

import argparse
import socket
import struct
import sys
import os
import select
import wad
import audio

from typing import Optional

# Constants
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 65432
EVENT_DEVICE = "/dev/input/event0"
DEFAULT_WAD = "DOOM.WAD"

PRESS_IDENTIFIER = 254
RELEASE_IDENTIFIER = 255

# Linux input event constants
EV_KEY = 0x01

# Input event structure format (from linux/input.h)
# struct input_event {
#     struct timeval time;  // 8 bytes on 32-bit, 16 bytes on 64-bit
#     __u16 type;           // 2 bytes
#     __u16 code;           // 2 bytes
#     __s32 value;          // 4 bytes
# };
# For 64-bit systems: 'QQHHi' (8+8+2+2+4 = 24 bytes)
INPUT_EVENT_FORMAT = 'QQHHi'
INPUT_EVENT_SIZE = struct.calcsize(INPUT_EVENT_FORMAT)


class InputEventClient:
    def __init__(self, audio: audio.AudioPlayerPool, host: str, port: int, device: str, verbose: bool = False):
        self.audio = audio
        self.host = host
        self.port = port
        self.device = device
        self.verbose = verbose
        self.event_fd: Optional[int] = None
        self.sock: Optional[socket.socket] = None

    def open_device(self) -> None:
        """Open the input device file."""
        try:
            self.event_fd = os.open(self.device, os.O_RDONLY)
        except OSError as e:
            print(f"Failed to open input device '{self.device}': {e}", file=sys.stderr)
            sys.exit(1)

    def connect_to_server(self) -> None:
        """Connect to the remote server."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
        except socket.error as e:
            print(f"Connection failed: {e}", file=sys.stderr)
            self.cleanup()
            sys.exit(1)

    def read_input_event(self) -> Optional[tuple]:
        """Read a single input event from the device."""
        try:
            data = os.read(self.event_fd, INPUT_EVENT_SIZE)
            if len(data) != INPUT_EVENT_SIZE:
                return None

            # Unpack the input event structure
            tv_sec, tv_usec, event_type, code, value = struct.unpack(INPUT_EVENT_FORMAT, data)
            return event_type, code, value
        except OSError as e:
            print(f"Error reading input event: {e}", file=sys.stderr)
            return None

    def send_key_event(self, is_press: bool, code: int) -> bool:
        """Send a key event to the server."""
        if code > 255:
            print(f"Key code {code} too large, skipping")
            return True

        identifier = PRESS_IDENTIFIER if is_press else RELEASE_IDENTIFIER
        buffer = bytes([identifier, code])

        try:
            sent = self.sock.send(buffer)
            if sent != len(buffer):
                print("Failed to send complete data", file=sys.stderr)
                return False
            return True
        except socket.error as e:
            print(f"Failed to send data: {e}", file=sys.stderr)
            return False

    def handle_server_data(self) -> bool:
        """Handle incoming data from server."""
        try:
            data = self.sock.recv(1024)
            if not data:
                print("Server closed connection")
                return False

            if self.verbose:
                print(f"Received from server: {data.decode('utf-8')}")

            # split data into lines
            lines = data.split(b'\n')
            for line in lines:
                if line.startswith(b'P'):
                    try:
                        n = int(line[1:])
                        # It's the next WAD for some reason
                        self.audio.play_sound(n+1)
                    except:
                        pass

            return True
        except socket.error as e:
            print(f"Error receiving from server: {e}", file=sys.stderr)
            return False

    def run(self) -> None:
        """Main event loop."""
        self.open_device()
        self.connect_to_server()

        print(f"Connected to {self.host}:{self.port}, reading from {self.device}")
        if self.verbose:
            print("Verbose mode enabled")

        try:
            while True:
                # Use select to poll both input device and network socket
                ready_fds, _, error_fds = select.select(
                    [self.event_fd, self.sock.fileno()],  # Read list
                    [],                                    # Write list
                    [self.sock.fileno()],                 # Error list
                    0.1                                   # Timeout (100ms)
                )

                # Check for socket errors
                if error_fds:
                    print("Socket error detected")
                    break

                # Handle input events
                if self.event_fd in ready_fds:
                    event_data = self.read_input_event()
                    if event_data is None:
                        break

                    event_type, code, value = event_data

                    # Check if the event is a key event
                    if event_type == EV_KEY:
                        if value == 1:  # Key press
                            if self.verbose:
                                print(f"Key Down: {code}")
                            if not self.send_key_event(True, code):
                                break
                        elif value == 0:  # Key release
                            if self.verbose:
                                print(f"Key Up: {code}")
                            if not self.send_key_event(False, code):
                                break
                        elif value == 2:  # Key auto repeat
                            # Ignore auto-repeat events
                            continue
                        else:
                            if self.verbose:
                                print(f"Unknown key event value: {value}")
                            continue

                # Handle server data
                if self.sock.fileno() in ready_fds:
                    if not self.handle_server_data():
                        break

        except KeyboardInterrupt:
            print("\nInterrupted by user")
        finally:
            self.cleanup()

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.event_fd is not None:
            try:
                os.close(self.event_fd)
            except OSError:
                pass
            self.event_fd = None

        if self.sock is not None:
            try:
                self.sock.close()
            except socket.error:
                pass
            self.sock = None


def main():
    parser = argparse.ArgumentParser(
        description="Forward input events from a Linux input device to a remote server"
    )
    parser.add_argument(
        "-H", "--host",
        default=SERVER_HOST,
        help=f"Specify the hostname (default: {SERVER_HOST})"
    )
    parser.add_argument(
        "-p", "--port",
        type=int,
        default=SERVER_PORT,
        help=f"Specify the port (default: {SERVER_PORT})"
    )
    parser.add_argument(
        "-d", "--device",
        default=EVENT_DEVICE,
        help=f"Specify the device name (default: {EVENT_DEVICE})"
    )
    parser.add_argument(
        "-w", "--wad",
        default=DEFAULT_WAD,
        help=f"Specify the DOOM WAD file (default: {DEFAULT_WAD})"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Enable verbose output"
    )

    args = parser.parse_args()

    w = wad.Wad(args.wad)
    s = audio.AudioPlayerPool(w.lumps)

    # Check if device exists
    if not os.path.exists(args.device):
        print(f"Error: Device '{args.device}' does not exist", file=sys.stderr)
        sys.exit(1)

    # Check if we have read permission
    if not os.access(args.device, os.R_OK):
        print(f"Error: No read permission for device '{args.device}'", file=sys.stderr)
        print("Try running with sudo or adding your user to the input group", file=sys.stderr)
        sys.exit(1)

    client = InputEventClient(s, args.host, args.port, args.device, args.verbose)
    client.run()


if __name__ == "__main__":
    main()
