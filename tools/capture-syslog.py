"""Receive Mocha TCP syslog; sends discovery and heartbeat only, never commands."""
import argparse
import socket
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=int, default=1800)
    args = parser.parse_args()
    deadline = time.monotonic() + args.seconds
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with socket.socket() as listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        listener.bind(("0.0.0.0", 7965))
        listener.listen(1)
        listener.settimeout(1)
        print("Listening on TCP 7965; discovering " + args.host, flush=True)
        with args.output.open("ab", buffering=0) as log:
            while time.monotonic() < deadline:
                udp.sendto(b"WIIU_TCP_SYSLOG_BEACON_V1", (args.host, 4445))
                try:
                    connection, peer = listener.accept()
                except socket.timeout:
                    continue
                with connection:
                    if peer[0] != args.host:
                        continue
                    print("Connected: " + peer[0], flush=True)
                    connection.settimeout(0.5)
                    heartbeat = 0.0
                    try:
                        while time.monotonic() < deadline:
                            if time.monotonic() >= heartbeat:
                                connection.sendall(b"\0")
                                heartbeat = time.monotonic() + 1
                            try:
                                data = connection.recv(65536)
                            except socket.timeout:
                                continue
                            if not data:
                                break
                            log.write(data)
                    except OSError as error:
                        print("Disconnected: " + str(error), flush=True)
            print("Capture complete: " + str(args.output), flush=True)


if __name__ == "__main__":
    main()
