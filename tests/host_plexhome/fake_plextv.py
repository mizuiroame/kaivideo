"""A made-up plex.tv and Plex server for host_plexhome:  fake_plextv.py PORT
plex.tv's Plex Home answers (/api/v2/home/users, .../switch, /api/v2/resources,
/api/v2/user) and the server's /identity, all on 127.0.0.1:PORT (the server
under /pms). Names, ids, tokens and the PIN are made up."""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(sys.argv[1])
SERVER_URI = f"http://127.0.0.1:{PORT}/pms"
PIN = "2468"
WRONG_LIMIT = 3  # Then 429, as plex.tv does after too many wrong PINs.

USERS = [
    {"id": 2, "uuid": "kid1", "title": "Kids", "username": "", "thumb": "", "restricted": True, "admin": False,
     "guest": False, "protected": True},
    {"id": 1, "uuid": "own1", "title": "Owner", "username": "owner-made-up", "thumb": "", "restricted": False,
     "admin": True, "guest": False, "protected": False},
    {"id": 3, "uuid": "teen1", "title": "Teen", "username": "", "thumb": "", "restricted": True, "admin": False,
     "guest": False, "protected": False},
    {"id": 4, "uuid": "nosrv1", "title": "No server", "username": "", "thumb": "", "restricted": True,
     "admin": False, "guest": False, "protected": False},
]
# Each profile's token for plex.tv, and the one plex.tv gives it for the server (None: not shared with it).
TOKENS = {"admin-token": "own1", "kid1-token": "kid1", "teen1-token": "teen1", "nosrv1-token": "nosrv1"}
SERVER_TOKENS = {"own1": "srv-owner", "kid1": "srv-kid", "teen1": "srv-teen", "nosrv1": None}
STATE = {"wrong": 0}


def resource(uuid):
    token = SERVER_TOKENS[uuid]
    if token is None:  # Only another server, not the one in use.
        return [{"name": "Elsewhere", "provides": "server", "clientIdentifier": "srv2", "accessToken": "x",
                 "owned": False, "connections": [{"uri": "http://127.0.0.1:9/pms", "address": "127.0.0.1",
                                                  "port": 9, "local": True, "relay": False}]}]
    return [{"name": "Test Server", "provides": "server", "clientIdentifier": "srv1", "accessToken": token,
             "owned": uuid == "own1", "connections": [{"uri": SERVER_URI, "address": "127.0.0.1", "port": PORT,
                                                      "local": True, "relay": False, "protocol": "http"}]}]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def who(self, query):
        return TOKENS.get(query.get("X-Plex-Token", [""])[0])

    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        uuid = self.who(query)
        if url.path == "/pms/identity":
            ok = query.get("X-Plex-Token", [""])[0] in [t for t in SERVER_TOKENS.values() if t]
            return self.send(200 if ok else 401, {"MediaContainer": {"machineIdentifier": "srv1"}})
        if uuid is None:
            return self.send(401, {"errors": [{"code": 1001, "message": "User could not be authenticated"}]})
        if url.path == "/api/v2/home/users":
            return self.send(200, {"id": 7, "name": "Made-up Home", "users": USERS})
        if url.path == "/api/v2/resources":
            return self.send(200, resource(uuid))
        if url.path == "/api/v2/user":
            user = next(u for u in USERS if u["uuid"] == uuid)
            return self.send(200, {"uuid": uuid, "username": user["username"], "title": user["title"]})
        self.send(404, {})

    def do_POST(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        if self.who(query) is None:
            return self.send(401, {})
        m = re.fullmatch(r"/api/v2/home/users/([A-Za-z0-9-]+)/switch", url.path)
        user = next((u for u in USERS if m and u["uuid"] == m.group(1)), None)
        if not user:
            return self.send(404, {"errors": [{"code": 1002, "message": "Not found"}]})
        if user["protected"]:
            if STATE["wrong"] >= WRONG_LIMIT:
                return self.send(429, {"errors": [{"code": 1003, "message": "Too many attempts"}]})
            if query.get("pin", [""])[0] != PIN:
                STATE["wrong"] += 1
                return self.send(403, {"errors": [{"code": 1041, "message": "Invalid PIN"}]})
            STATE["wrong"] = 0
        token = f"{user['uuid']}-token" if user["uuid"] != "own1" else "admin-token"
        self.send(201, {"id": user["id"], "uuid": user["uuid"], "authToken": token, "authenticationToken": token})


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
