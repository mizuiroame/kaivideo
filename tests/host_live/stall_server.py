"""A web folder that stalls on purpose, for trying KaiVideo's wait for the network on the console.

It serves a folder as a plain index page (Apache-like), with Range, and every request whose range
starts in the middle of a file (between 40% and 60% of it) waits --stall seconds first, as a slow
network or server would. Made-up videos only.

    python stall_server.py --media ../../testvideos/stall --port 8102 --stall 4

On the console: Settings > Network folder > http://<this PC>:8102/
"""

from __future__ import annotations

import argparse
import html
import os
import time
import urllib.parse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHUNK = 64 * 1024


class Handler(BaseHTTPRequestHandler):
    media = "."
    stall = 4.0

    def log_message(self, fmt: str, *args) -> None:
        pass

    def do_GET(self) -> None:
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        root = os.path.realpath(self.media)
        full = os.path.realpath(os.path.join(root, path.lstrip("/")))
        if full != root and not full.startswith(root + os.sep):
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        if os.path.isdir(full):
            self.index(full, path)
        elif os.path.isfile(full):
            self.file(full)
        else:
            self.send_error(HTTPStatus.NOT_FOUND)

    def index(self, full: str, path: str) -> None:
        if not path.endswith("/"):
            path += "/"
        links = "".join(
            f'<a href="{urllib.parse.quote(name)}{"/" if os.path.isdir(os.path.join(full, name)) else ""}">'
            f"{html.escape(name)}</a>\n"
            for name in sorted(os.listdir(full))
        )
        body = f"<html><head><title>Index of {html.escape(path)}</title></head><body><pre>{links}</pre></body></html>"
        data = body.encode()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def file(self, full: str) -> None:
        size = os.path.getsize(full)
        start, end = 0, size - 1
        ranged = self.headers.get("Range", "")
        if ranged.startswith("bytes="):
            first, _, last = ranged[6:].split(",")[0].partition("-")
            if first:
                start = int(first)
                end = min(int(last), size - 1) if last else size - 1
        if 0.4 * size <= start < 0.6 * size:
            print(f"stalling {self.stall:.0f} s at {start / size:.0%} of {os.path.basename(full)}", flush=True)
            time.sleep(self.stall)
        self.send_response(HTTPStatus.PARTIAL_CONTENT if ranged else HTTPStatus.OK)
        self.send_header("Content-Type", "video/x-matroska")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if ranged:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        with open(full, "rb") as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                piece = f.read(min(CHUNK, left))
                if not piece:
                    break
                try:
                    self.wfile.write(piece)
                except (BrokenPipeError, ConnectionResetError):
                    break
                left -= len(piece)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--media", required=True)
    parser.add_argument("--port", type=int, default=8102)
    parser.add_argument("--stall", type=float, default=4.0)
    args = parser.parse_args()
    Handler.media = args.media
    Handler.stall = args.stall
    print(f"Serving {args.media} on port {args.port}, stalling {args.stall:.0f} s in the middle of files", flush=True)
    ThreadingHTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
