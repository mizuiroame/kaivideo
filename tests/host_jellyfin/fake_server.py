"""A small stand-in for a Jellyfin server, for host_jellyfin:  fake_server.py PORT VIDEO LOG BIG_FILE
While the file LOG.fail exists, reading videos fails."""
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, VIDEO, LOG, BIG = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
SIZE = os.path.getsize(VIDEO)
TOKEN = "fake-token"
ITEM = {
    "Id": "aaaa1111", "Name": "Paper Lanterns", "Type": "Movie", "ProductionYear": 1998,
    "Overview": "A made-up film.", "RunTimeTicks": 100000000, "DateCreated": "2024-03-09T21:15:00Z",
    "UserData": {"PlaybackPositionTicks": 0, "Played": False},
    "MediaSources": [{"Id": "aaaa2222", "Size": SIZE, "Bitrate": 2000000, "Container": "mkv", "MediaStreams": [
        {"Type": "Video", "Codec": "hevc", "Width": 1920, "BitDepth": 10, "VideoRange": "SDR"}]}],
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, body, status=200):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def signed(self):
        return f'Token="{TOKEN}"' in self.headers.get("Authorization", "")

    def note(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode() if length else ""
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(f"{self.command} {self.path.split('?')[0]} {body}\n")
        return body

    def stream(self):
        match = re.match(r"bytes=(\d+)-(\d*)", self.headers.get("Range", ""))
        if not match or os.path.exists(LOG + ".fail"):
            self.reply({}, 500)
            return
        name = BIG if "/Videos/big/" in self.path else VIDEO
        size = os.path.getsize(name)
        start = int(match[1])
        end = min(int(match[2]) if match[2] else size - 1, size - 1)
        self.send_response(206)
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        with open(name, "rb") as f:
            f.seek(start)
            self.wfile.write(f.read(end - start + 1))

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/QuickConnect/Connect":
            self.reply({"Authenticated": True})
        elif not self.signed():
            self.reply({}, 401)
        elif path == "/Items":
            self.reply({"TotalRecordCount": 1, "Items": [ITEM]})
        elif "/stream" in path:
            self.stream()
        else:
            self.reply({}, 404)

    def do_POST(self):
        self.note()
        path = self.path.split("?")[0]
        if path == "/QuickConnect/Initiate":
            self.reply({"Secret": "s3cret", "Code": "123456"})
        elif path == "/Users/AuthenticateWithQuickConnect":
            self.reply({"AccessToken": TOKEN, "User": {"Id": "user1", "Name": "Ana"}})
        else:
            self.reply({})

    def do_DELETE(self):
        self.note()
        self.reply({})


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
