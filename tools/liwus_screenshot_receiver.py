#!/usr/bin/env python3
"""Receive one BMP screenshot per TCP connection from LIWUS OS."""

import argparse
import datetime as dt
import ipaddress
import socket
import struct
from pathlib import Path

HEADER_SIZE = 54
DEFAULT_PORT = 49152
DEFAULT_MAX_BYTES = 32 * 1024 * 1024


def local_ipv4_addresses():
    found = set()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("192.0.2.1", 9))
            found.add(probe.getsockname()[0])
    except OSError:
        pass
    try:
        for result in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            found.add(result[4][0])
    except OSError:
        pass
    return sorted(address for address in found if not address.startswith("127."))


def recv_exact(conn, count):
    parts = bytearray()
    while len(parts) < count:
        chunk = conn.recv(min(65536, count - len(parts)))
        if not chunk:
            raise ConnectionError(f"connection ended after {len(parts)} of {count} bytes")
        parts.extend(chunk)
    return bytes(parts)


def validate_bmp_header(header, max_bytes):
    if len(header) != HEADER_SIZE or header[:2] != b"BM":
        raise ValueError("not a 24-bit BMP screenshot")
    file_size, pixel_offset = struct.unpack_from("<IxxxxI", header, 2)
    dib_size, width, height, planes, depth, compression, image_size = struct.unpack_from(
        "<IiiHHII", header, 14
    )
    if dib_size != 40 or pixel_offset != HEADER_SIZE:
        raise ValueError("unsupported BMP header")
    if width <= 0 or height == 0 or abs(height) > 8192 or width > 8192:
        raise ValueError("invalid BMP dimensions")
    if planes != 1 or depth != 24 or compression != 0:
        raise ValueError("expected uncompressed 24-bit BMP")
    stride = (width * 3 + 3) & ~3
    expected_image_size = stride * abs(height)
    if image_size != expected_image_size or file_size != HEADER_SIZE + expected_image_size:
        raise ValueError("BMP size fields do not match dimensions")
    if file_size > max_bytes:
        raise ValueError(f"image is too large ({file_size} bytes; limit {max_bytes})")
    return file_size, width, abs(height)


def receive_one(conn, output_dir, max_bytes):
    conn.settimeout(60)
    header = recv_exact(conn, HEADER_SIZE)
    file_size, width, height = validate_bmp_header(header, max_bytes)
    image = header + recv_exact(conn, file_size - HEADER_SIZE)

    now = dt.datetime.now().astimezone()
    filename = now.strftime("liwus-%Y%m%d-%H%M%S-%f.bmp")
    destination = output_dir / filename
    partial = destination.with_suffix(destination.suffix + ".part")
    partial.write_bytes(image)
    partial.replace(destination)
    conn.sendall(b"OK\n")
    return destination, width, height, file_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="listen address (default: all interfaces)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--output-dir", type=Path, default=Path("liwus-captures"))
    parser.add_argument("--max-mib", type=int, default=32)
    args = parser.parse_args()
    ipaddress.IPv4Address(args.host)
    if not 1 <= args.port <= 65535 or not 1 <= args.max_mib <= 256:
        parser.error("port or image-size limit is out of range")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(2)
        print(f"LIWUS screenshot receiver listening on {args.host}:{args.port}")
        for address in local_ipv4_addresses():
            print(f"Run on LIWUS: screenshot {address} {args.port}")
        print(f"Images will be saved in: {args.output_dir.resolve()}")
        print("Press Ctrl+C to stop.")
        try:
            while True:
                conn, peer = server.accept()
                with conn:
                    try:
                        destination, width, height, size = receive_one(
                            conn, args.output_dir, args.max_mib * 1024 * 1024
                        )
                        print(f"Saved {width}x{height} ({size} bytes) from {peer[0]}: {destination}")
                    except (ConnectionError, OSError, ValueError) as exc:
                        print(f"Rejected capture from {peer[0]}: {exc}")
                        try:
                            conn.sendall(b"NO\n")
                        except OSError:
                            pass
        except KeyboardInterrupt:
            print("\nReceiver stopped.")


if __name__ == "__main__":
    main()
