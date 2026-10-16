"""Send an XAVATAR ELF to an already-running compatible PS4 payload loader."""
import argparse
from pathlib import Path
import socket
import sys


def send_payload(payload: Path, host: str, port: int, timeout: float = 20) -> int:
    if not 1 <= port <= 65535:
        raise ValueError('Port must be between 1 and 65535.')
    with payload.open('rb') as source:
        if source.read(4) != b'\x7fELF':
            raise ValueError('Choose an XAVATAR .elf file.')
        source.seek(0)
        sent = 0
        with socket.create_connection((host, port), timeout=timeout) as connection:
            while chunk := source.read(65536):
                connection.sendall(chunk)
                sent += len(chunk)
    return sent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='PS4 IP address')
    parser.add_argument('--port', type=int, default=9021, help='ELF loader port (default: 9021)')
    parser.add_argument('--file', type=Path, help='Payload file; defaults to XAVATAR-installer.elf')
    args = parser.parse_args()
    payload = args.file
    if payload is None:
        root = Path(__file__).resolve().parent
        payload = next((p for p in (root / 'XAVATAR-installer.elf', root / 'build/XAVATAR-installer.elf') if p.is_file()), root / 'XAVATAR-installer.elf')
    try:
        count = send_payload(payload, args.host, args.port)
    except (OSError, ValueError) as error:
        print(f'Could not send the payload: {error}', file=sys.stderr)
        return 1
    print(f'Sent {payload.name} ({count:,} bytes) to {args.host}:{args.port}.')
    print('Wait for the PS4 notification, then open http://' + args.host + ':4002')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
