#!/usr/bin/env python3
# Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
"""Host tests only: a tiny IPP printer on 127.0.0.1 that asks for a login.

    auth_printer.py PORT CERT KEY LOG [--plain] [--basic]

With CERT and KEY it speaks IPPS (TLS on PORT); with --plain it speaks
plain IPP. Every request without a good Authorization gets 401 and a
Digest challenge (MD5, qop=auth), or a Basic one with --basic. The login
it takes is kim / "Circle Of Life". Each request is logged to LOG as one
line: the operation, and whether an Authorization header came and was
good. Nothing leaves this machine; standard library only.
"""
import base64
import hashlib
import socket
import ssl
import struct
import sys
import threading

USER, PASSWORD, REALM, NONCE = "kim", "Circle Of Life", "OpenPrint test", "dcd98b7102dd2f0e8b11d0f600bfb0c093"


def md5(text):
    return hashlib.md5(text.encode()).hexdigest()


def params(value):
    out, rest = {}, value.split(" ", 1)[1] if " " in value else ""
    for part in rest.split(","):
        if "=" in part:
            k, v = part.strip().split("=", 1)
            out[k.strip()] = v.strip().strip('"')
    return out


def authorized(header, basic):
    if not header:
        return False
    if basic:
        return header.startswith("Basic ") and base64.b64decode(header[6:]).decode() == f"{USER}:{PASSWORD}"
    if not header.startswith("Digest "):
        return False
    p = params(header)
    ha1 = md5(f"{USER}:{REALM}:{PASSWORD}")
    ha2 = md5(f"POST:{p.get('uri', '')}")
    want = md5(f"{ha1}:{NONCE}:{p.get('nc', '')}:{p.get('cnonce', '')}:auth:{ha2}")
    return p.get("username") == USER and p.get("nonce") == NONCE and p.get("response") == want


def attr(tag, name, value):
    return struct.pack(">BH", tag, len(name)) + name.encode() + struct.pack(">H", len(value)) + value


def ipp_reply(op, request_id):
    out = struct.pack(">BBHI", 1, 1, 0, request_id) + b"\x01"
    out += attr(0x47, "attributes-charset", b"utf-8") + attr(0x48, "attributes-natural-language", b"en")
    if op == 0x000B:
        out += b"\x04" + attr(0x49, "document-format-supported", b"application/pdf")
        out += attr(0x22, "printer-is-accepting-jobs", b"\x01") + attr(0x23, "printer-state", struct.pack(">I", 3))
        out += attr(0x22, "color-supported", b"\x01") + attr(0x41, "printer-make-and-model", b"OpenPrint login test")
    else:
        out += b"\x02" + attr(0x21, "job-id", struct.pack(">I", 7))
    return out + b"\x03"


def serve(conn, log, basic):
    with conn:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = conn.recv(4096)
            if not chunk:
                return
            data += chunk
        head, body = data.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        headers = {k.strip().lower(): v.strip() for k, v in (l.split(":", 1) for l in lines[1:] if ":" in l)}
        length = int(headers.get("content-length", "0"))
        while len(body) < length:
            chunk = conn.recv(65536)
            if not chunk:
                break
            body += chunk
        op, request_id = struct.unpack(">HI", body[2:8]) if len(body) >= 8 else (0, 0)
        auth = headers.get("authorization")
        good = authorized(auth, basic)
        with open(log, "a") as f:
            f.write(f"op={op:#06x} auth={'none' if not auth else ('good' if good else 'bad')} bytes={len(body)}\n")
        if not good:
            challenge = f'Basic realm="{REALM}"' if basic else f'Digest realm="{REALM}", nonce="{NONCE}", qop="auth", algorithm=MD5'
            conn.sendall(f"HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: {challenge}\r\nContent-Length: 0\r\n"
                         "Connection: close\r\n\r\n".encode())
            return
        reply = ipp_reply(op, request_id)
        conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
                     % len(reply) + reply)


def main():
    port, cert, key, log = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
    plain, basic = "--plain" in sys.argv, "--basic" in sys.argv
    context = None
    if not plain:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(8)
    while True:
        conn, _ = listener.accept()
        try:
            if context:
                conn = context.wrap_socket(conn, server_side=True)
        except (ssl.SSLError, OSError):
            conn.close()
            continue
        threading.Thread(target=serve, args=(conn, log, basic), daemon=True).start()


if __name__ == "__main__":
    main()
