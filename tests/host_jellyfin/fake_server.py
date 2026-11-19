"""A small stand-in for a Jellyfin server, for host_jellyfin:  fake_server.py PORT VIDEO LOG BIG_FILE HLS_FOLDER
While the file LOG.fail exists, reading videos fails. While LOG.slow exists, the
second HLS segment doesn't come, as when the server hasn't converted it yet."""
import json
import os
import re
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, VIDEO, LOG, BIG, HLS_DIR = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
LIVE_FIRST_READ = None
LIVE_SECOND_AFTER_S = 4.0
SLOW_MAX_S = 20.0
SLOW_STEP_S = 0.05
SIZE = os.path.getsize(VIDEO)
TOKEN = "fake-token"
ITEM = {
    "Id": "aaaa1111", "Name": "Paper Lanterns", "Type": "Movie", "ProductionYear": 1998,
    "Overview": "A made-up film.", "RunTimeTicks": 100000000, "DateCreated": "2024-03-09T21:15:00Z",
    "UserData": {"PlaybackPositionTicks": 0, "Played": False, "IsFavorite": True},
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

    def hls(self, name):
        waited = 0.0
        while name.endswith("seg1.ts") and os.path.exists(LOG + ".slow") and waited < SLOW_MAX_S:
            time.sleep(SLOW_STEP_S)
            waited += SLOW_STEP_S
        with open(os.path.join(HLS_DIR, os.path.basename(name)), "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def live_playlist(self):
        """Live TV: a list that grows (no end), with the second segment a few seconds later."""
        global LIVE_FIRST_READ
        if LIVE_FIRST_READ is None:
            LIVE_FIRST_READ = time.time()
        lines = ["#EXTM3U", "#EXT-X-VERSION:3", "#EXT-X-TARGETDURATION:8", "#EXT-X-MEDIA-SEQUENCE:0",
                 "#EXT-X-PLAYLIST-TYPE:EVENT", "#EXTINF:8.333333,", "/hls/seg0.ts"]
        if time.time() - LIVE_FIRST_READ > LIVE_SECOND_AFTER_S:
            lines += ["#EXTINF:1.666667,", "/hls/seg1.ts"]
        data = ("\n".join(lines) + "\n").encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/hlslive/master.m3u8":
            data = b"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000\nlive.m3u8\n"
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif path == "/hlslive/live.m3u8":
            self.live_playlist()
        elif path.startswith("/hls/"):
            self.hls(path)
        elif path == "/System/Info/Public":
            self.reply({"ServerName": "Fake Den", "Version": "10.11.0", "ProductName": "Jellyfin Server"})
        elif path == "/QuickConnect/Connect":
            self.reply({"Authenticated": True})
        elif not self.signed():
            self.reply({}, 401)
        elif path == "/Items" and "Fields=Trickplay" in self.path:
            sizes = {"40": {"Width": 40, "Height": 20, "TileWidth": 2, "TileHeight": 2, "ThumbnailCount": 4,
                            "Interval": 1000}}
            self.reply({"Items": [{"Id": "aaaa1111", "Trickplay": {"aaaa2222": sizes}}]})
        elif path == "/Videos/aaaa1111/Trickplay/40/0.jpg":
            with open(os.path.join(os.path.dirname(__file__), "trickplay_sheet.jpg"), "rb") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif path == "/MediaSegments/aaaa1111":
            self.reply({"Items": [{"Type": "Intro", "StartTicks": 0, "EndTicks": 50000000}]})
        elif path == "/UserViews":
            self.reply({"Items": [{"Id": "view1", "Name": "Fake Films", "CollectionType": "movies"}]})
        elif path == "/Items" and "IncludeItemTypes=Series" in self.path:
            self.reply({"Items": []})  # No favorite series.
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


def answer_discovery():
    """Answers "Who is JellyfinServer?" as a server in Docker does: with its inner address."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", DISCOVERY_PORT))
    while True:
        data, sender = sock.recvfrom(2048)
        if data.startswith(b"Who is JellyfinServer?"):
            reply = {"Address": f"http://172.18.0.11:{PORT}", "Id": "fake", "Name": "Fake Den"}
            sock.sendto(json.dumps(reply).encode(), sender)


DISCOVERY_PORT = 7359
threading.Thread(target=answer_discovery, daemon=True).start()
ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
