"""Serves the shipping OTA page with a scriptable backend.

The page's failure paths cannot be produced by a real node on demand -- an HTTP 500 or a
dropped connection mid-upload is exactly what a working device never does. Endpoints match
what the firmware serves: /update/identity, /update, /update/time, /update/advert.
"""
import http.server, json, os, socketserver, sys, threading

PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "..", "web", "ota-page.min.html")

# Mirrors the payload HotspotOtaIntegration builds. cid 9 is ESP32-S3 (heltec_v4).
IDENTITY = {
    "nm": "TestNode", "id": "a1b2c3d4", "k": "ff00", "hw": "Heltec V4.3 OLED",
    "cid": 9, "cap": 3145728,
    "ta": "Slot A", "tv": "v1.17.1-aaaaaaa",
    "ra": "Slot B", "rv": "v1.17.1-bbbbbbb", "rt": "recorded-valid",
    "role": "repeater", "ver": "v1.17.1-bbbbbbb", "sha": "bbbbbbb",
    "t": 1788900000, "mv": 3900,
}

# Set by the test through /__control to steer the next upload.
STATE = {"upload": "ok"}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body=b"", ctype="text/plain"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/__control"):
            STATE["upload"] = self.path.split("=")[-1]
            return self._send(200, b"set")
        if self.path.startswith("/update/identity"):
            if STATE.get("identity") == "fail":
                return self._send(500, b"nope")
            return self._send(200, json.dumps(IDENTITY).encode(), "application/json")
        if self.path in ("/", "/update", "/index.html"):
            with open(PAGE, "rb") as f:
                return self._send(200, f.read(), "text/html")
        self._send(404)

    def do_POST(self):
        if self.path.startswith("/update/time") or self.path.startswith("/update/advert"):
            return self._send(200, b"OK")
        if self.path == "/update":
            mode = STATE["upload"]
            if mode == "drop":
                # Reset mid-request so the page sees xhr.onerror. Shutting the socket
                # avoids socketserver's own flush raising on a closed wfile.
                import socket
                self.close_connection = True
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                return
            length = int(self.headers.get("Content-Length", 0))
            remaining = length
            while remaining > 0:
                chunk = self.rfile.read(min(65536, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
            if mode == "http500":
                return self._send(500, b"flash write failed")
            return self._send(200, b"OK - update installed")
        self._send(404)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def serve(port=8931):
    srv = Server(("127.0.0.1", port), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8931
    serve(port)
    print(f"mock OTA backend on {port}")
    threading.Event().wait()
