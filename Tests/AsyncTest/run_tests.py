#!/usr/bin/env python3
"""Loopback-only HTTP/TLS fixtures. Generates an ephemeral CA outside production."""
import argparse
from contextlib import ExitStack
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import struct
import zlib
from urllib.parse import parse_qs, urlsplit


class Server(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False
    allow_reuse_address = True
    next_connection = 0
    counter_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        with Server.counter_lock:
            Server.next_connection += 1
            self.connection_id = Server.next_connection

    def log_message(self, *_args):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError, OSError):
            pass

    def respond(self, body, status=200, close=False):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("X-Connection-ID", str(self.connection_id))
        if close:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)
            self.wfile.flush()

    def do_GET(self):
        path = urlsplit(self.path)
        query = parse_qs(path.query)
        body = b""
        if self.headers.get("Content-Length"):
            body = self.rfile.read(int(self.headers["Content-Length"]))
        route = path.path
        if route.startswith("/image/"):
            self.image(route, query)
        elif route == "/drop":
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
        elif route == "/slow":
            time.sleep(min(5, max(0, int(query.get("ms", ["300"])[0])) / 1000))
            self.respond(b"slow")
        elif route == "/echo":
            self.respond(body)
        elif route == "/connection-id":
            self.respond(str(self.connection_id).encode())
        elif route == "/binary":
            self.respond(b"a\x00b\xffc")
        elif route == "/large":
            self.respond(b"x" * min(8 * 1024 * 1024, int(query.get("bytes", ["262144"])[0])))
        elif route == "/split":
            chunks = [b"HTTP/1.1 2", b"00 OK\r\nContent-L", b"ength: 5\r\n\r\nhe", b"llo"]
            for chunk in chunks:
                self.wfile.write(chunk)
                self.wfile.flush()
                time.sleep(0.003)
        elif route == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Trailer", "X-Test")
            self.end_headers()
            for chunk in (b"2\r\nhe\r\n", b"3\r\nllo\r\n", b"0\r\nX-Test: done\r\n\r\n"):
                self.wfile.write(chunk)
                self.wfile.flush()
                time.sleep(0.002)
        elif route == "/truncated":
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"short")
            self.close_connection = True
        elif route == "/eof":
            self.wfile.write(b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\neof-body")
            self.wfile.flush()
            self.close_connection = True
        elif route == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/value")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif route.startswith("/status/"):
            self.respond(b"status-body", int(route.rsplit("/", 1)[1]))
        elif route == "/close":
            self.respond(b"closed", close=True)
        else:
            self.respond(b"21")

    def image(self, route, query):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        width = int(query.get("w", ["13"])[0])
        height = int(query.get("h", ["7"])[0])
        payload = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                   + chunk(b"IDAT", zlib.compress((b"\0" + b"\x12\x34\x56" * width) * height)) + chunk(b"IEND", b""))
        if route in ("/image/redirect", "/image/loop", "/image/down", "/image/relative", "/image/queryredirect", "/image/queryrelative"):
            target = {"/image/redirect": "/image/png?w=17&h=9", "/image/loop": "/image/loop",
                      "/image/down": "http://127.0.0.1/forbidden", "/image/relative": "nested/../png?w=17&h=9",
                      "/image/queryredirect": "/image/png?w=17&source=https://cdn.example/a",
                      "/image/queryrelative": "png?w=17&source=https://cdn.example/a"}[route]
            self.send_response(302)
            self.send_header("Location", target)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if route == "/image/slow":
            time.sleep(0.5)
        if route == "/image/large":
            payload += b"\0" * 100000
        if route == "/image/late":
            # JPEG metadata after an APP segment, beyond a small Range probe.
            payload = (b"\xff\xd8\xff\xe1" + struct.pack(">H", 1002) + b"x" * 1000
                       + b"\xff\xc0\x00\x11\x08" + struct.pack(">HH", height, width)
                       + b"\x03\x01\x11\x00\x02\x11\x00\x03\x11\x00\xff\xd9")
        if route == "/image/invalid":
            payload = b"<html>upstream failed</html>"
        if route == "/image/truncated":
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload) + 50))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)
            self.close_connection = True
            return
        status = 200
        range_header = None
        if self.headers.get("Range") and route != "/image/large":
            end = min(int(self.headers["Range"].split("-")[1]), len(payload) - 1)
            range_header = f"bytes {1 if route == '/image/badrange' else 0}-{end}/{len(payload)}"
            payload = payload[:end + 1]
            status = 206
        self.send_response(status)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Content-Type", "image/png" if route != "/image/late" else "image/jpeg")
        if range_header:
            self.send_header("Content-Range", range_header)
        if route == "/image/encoded":
            self.send_header("Content-Encoding", "gzip")
        self.end_headers()
        self.wfile.write(payload)

    do_POST = do_GET
    do_PUT = do_GET
    do_HEAD = do_GET


def certificates(directory, openssl):
    def run(*args):
        subprocess.run([openssl, *args], cwd=directory, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj", "/CN=FeiLib test CA",
        "-keyout", "ca.key", "-out", "ca.pem", "-addext", "basicConstraints=critical,CA:TRUE")
    run("req", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost", "-keyout", "server.key", "-out", "server.csr")
    (directory / "extensions.cnf").write_text("subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n", encoding="ascii")
    run("x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
        "-days", "2", "-out", "server.pem", "-extfile", "extensions.cnf")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--groups", nargs="+", default=["all"])
    args = parser.parse_args()
    executable = args.executable.resolve()
    # Use the ignored build directory: Windows sandbox temp folders may deny SQLite/file operations.
    with tempfile.TemporaryDirectory(prefix="async-fixture-", dir=executable.parent) as temp, ExitStack() as stack:
        directory = Path(temp)
        openssl = os.environ.get("OPENSSL_BIN") or shutil.which("openssl")
        if not openssl:
            raise RuntimeError("openssl executable is required for local test certificates")
        certificates(directory, openssl)
        http = Server(("127.0.0.1", 0), Handler)
        https = Server(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(directory / "server.pem", directory / "server.key")
        context.set_alpn_protocols(["http/1.1"])
        https.socket = context.wrap_socket(https.socket, server_side=True)
        threads = []
        for server in (http, https):
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            threads.append(thread)
            stack.callback(server.server_close)
            stack.callback(server.shutdown)
        env = os.environ.copy()
        env.update(FEILIB_TEST_HTTP_URL=f"http://127.0.0.1:{http.server_port}",
                   FEILIB_TEST_HTTPS_URL=f"https://localhost:{https.server_port}",
                   FEILIB_TEST_CA_FILE=str(directory / "ca.pem"),
                   BLOG_TEST_TEMP_DIR=str(directory))
        for group in args.groups:
            print(f"Running {group} with loopback-only fixtures", flush=True)
            subprocess.run([str(executable), group], env=env, cwd=executable.parent, check=True, timeout=120)


if __name__ == "__main__":
    main()
