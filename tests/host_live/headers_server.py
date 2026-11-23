"""A local server for trying stream headers on the console (KaiVideo 1.15.1).

Everything under /locked/ answers 403 unless the request carries the
Referer, User-Agent and Origin below; /open/ answers anyone. It serves:

  /list.m3u              an M3U list: #EXTVLCOPT, Kodi's "|" form, one
                         locked channel without headers (must fail) and an
                         open one (a control)
  /addon/manifest.json   a Stremio addon with one film whose streams ask for
                         the headers (proxyHeaders), one that asks for a
                         Cookie (left out by KaiVideo), and an open one

Media come from a folder made with ffmpeg (see --media): hls/index.m3u8 and
its segments, direct.ts and source.mp4. Made-up content only.

    python headers_server.py --media ../../testvideos/headers --host 192.168.0.10

Each request is printed with the header names it brought and whether they
matched; the values are never printed.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import posixpath
import urllib.parse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REFERER = "https://referer.test/"
USER_AGENT = "KaiVideoHeaderTest/1.0"
ORIGIN = "https://origin.test"
REQUIRED = {"Referer": REFERER, "User-Agent": USER_AGENT, "Origin": ORIGIN}
CHUNK = 64 * 1024


def m3u(base: str) -> str:
    pipe = "Referer={}&User-Agent={}&Origin={}".format(
        *(urllib.parse.quote(v, safe="") for v in (REFERER, USER_AGENT, ORIGIN))
    )
    return "\n".join(
        [
            "#EXTM3U",
            '#EXTINF:-1 group-title="Headers",1 Locked HLS (EXTVLCOPT)',
            f"#EXTVLCOPT:http-referrer={REFERER}",
            f"#EXTVLCOPT:http-user-agent={USER_AGENT}",
            f"#EXTVLCOPT:http-origin={ORIGIN}",
            f"{base}/locked/hls/index.m3u8",
            '#EXTINF:-1 group-title="Headers",2 Locked TS (Kodi pipe)',
            f"{base}/locked/direct.ts|{pipe}",
            '#EXTINF:-1 group-title="Headers",3 Locked, no headers (must fail)',
            f"{base}/locked/hls/index.m3u8",
            '#EXTINF:-1 group-title="Headers",4 Open HLS (control)',
            f"{base}/open/hls/index.m3u8",
            "",
        ]
    )


def addon(path: str, base: str) -> dict | None:
    film = {"id": "kvh1", "type": "movie", "name": "Headers Test", "description": "A made-up film for headers."}
    proxy = {"request": dict(REQUIRED)}
    answers = {
        "manifest.json": {
            "id": "test.kaivideo.headers",
            "version": "1.0.0",
            "name": "Headers Test",
            "description": "Streams that need headers (test).",
            "resources": ["catalog", "meta", "stream"],
            "types": ["movie"],
            "idPrefixes": ["kvh"],
            "catalogs": [{"type": "movie", "id": "headers", "name": "Headers test"}],
        },
        "catalog/movie/headers.json": {"metas": [film]},
        "meta/movie/kvh1.json": {"meta": film},
        "stream/movie/kvh1.json": {
            "streams": [
                {"url": f"{base}/locked/source.mp4", "name": "A locked MP4",
                 "title": "Needs Referer, User-Agent, Origin", "behaviorHints": {"proxyHeaders": proxy}},
                {"url": f"{base}/locked/hls/index.m3u8", "name": "B locked HLS",
                 "title": "Needs Referer, User-Agent, Origin", "behaviorHints": {"proxyHeaders": proxy}},
                {"url": f"{base}/locked/source.mp4", "name": "C cookie",
                 "title": "Asks for a Cookie: must not be listed",
                 "behaviorHints": {"proxyHeaders": {"request": {"Cookie": "session=1"}}}},
                {"url": f"{base}/open/source.mp4", "name": "D open MP4", "title": "No headers (control)"},
            ]
        },
    }
    return answers.get(path)


class Handler(BaseHTTPRequestHandler):
    media = "."
    base = ""

    def log_message(self, fmt: str, *args) -> None:  # One line per request, printed by handle_one below.
        pass

    def check(self) -> tuple[bool, str]:
        parts = []
        ok = True
        for name, want in REQUIRED.items():
            got = self.headers.get(name)
            mark = "ok" if got == want else ("wrong" if got else "missing")
            ok &= mark == "ok"
            parts.append(f"{name} {mark}")
        return ok, ", ".join(parts)

    def send_bytes(self, body: bytes, kind: str) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_file(self, relative: str) -> int:
        root = os.path.realpath(self.media)
        full = os.path.realpath(os.path.join(root, relative))
        if not full.startswith(root + os.sep) or not os.path.isfile(full):
            self.send_error(HTTPStatus.NOT_FOUND)
            return 404
        size = os.path.getsize(full)
        start, end = 0, size - 1
        ranged = self.headers.get("Range", "")
        if ranged.startswith("bytes="):
            first, _, last = ranged[6:].split(",")[0].partition("-")
            if first:
                start = int(first)
                end = min(int(last), size - 1) if last else size - 1
            elif last:
                start = max(0, size - int(last))
            if start > end or start >= size:
                self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                self.send_header("Content-Range", f"bytes */{size}")
                self.end_headers()
                return 416
        status = HTTPStatus.PARTIAL_CONTENT if ranged else HTTPStatus.OK
        kind = "application/vnd.apple.mpegurl" if full.endswith(".m3u8") else (
            mimetypes.guess_type(full)[0] or "application/octet-stream")
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        if ranged:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if self.command == "HEAD":
            return int(status)
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
        return int(status)

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        path = posixpath.normpath(urllib.parse.urlsplit(self.path).path)
        status, note = 200, ""
        if path == "/list.m3u":
            self.send_bytes(m3u(self.base).encode(), "audio/x-mpegurl")
        elif path.startswith("/addon/"):
            answer = addon(path[len("/addon/"):], self.base)
            if answer is None:
                self.send_error(HTTPStatus.NOT_FOUND)
                status = 404
            else:
                self.send_bytes(json.dumps(answer).encode(), "application/json")
        elif path.startswith("/locked/"):
            ok, note = self.check()
            if ok:
                status = self.send_file(path[len("/locked/"):])
            else:
                self.send_error(HTTPStatus.FORBIDDEN, "The stream's headers are missing")
                status = 403
        elif path.startswith("/open/"):
            status = self.send_file(path[len("/open/"):])
        else:
            self.send_error(HTTPStatus.NOT_FOUND)
            status = 404
        print(f"{status} {self.command} {path}" + (f"  [{note}]" if note else ""), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--media", required=True, help="folder with hls/, direct.ts and source.mp4")
    parser.add_argument("--host", required=True, help="this PC's address the console reaches")
    parser.add_argument("--port", type=int, default=8099)
    args = parser.parse_args()
    Handler.media = args.media
    Handler.base = f"http://{args.host}:{args.port}"
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"List:  {Handler.base}/list.m3u")
    print(f"Addon: {Handler.base}/addon/manifest.json", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
