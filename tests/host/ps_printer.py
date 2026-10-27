#!/usr/bin/env python3
# Copyright (c) 2026 AmigaChrome Team. SPDX-License-Identifier: MIT
"""A stand-in IPP printer for OpenPrint's tests: it prints nothing.

It answers Get-Printer-Attributes with the formats it is given, and keeps each
Print-Job's document as it came (spool/job-N.ps or .pdf) with the request's
attributes beside it (job-N.json: every attribute, the document's length and
SHA-256). A Print-Job in a format it doesn't take gets
client-error-document-format-not-supported (0x040a) and nothing is kept.

  ps_printer.py --bind ADDR --port N --spool DIR [--formats a,b] [--mode M] [--log FILE]

Modes: normal; refuse (lists the formats, then refuses every Print-Job with
0x040a, as a printer that claims more than it reads); silent (takes the
connection and the request and never answers). It binds only to ADDR: the
tests use 127.0.0.1, the lab a container's own address. Standard library only.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

OP_PRINT_JOB, OP_VALIDATE_JOB, OP_GET_PRINTER_ATTRIBUTES = 0x0002, 0x0004, 0x000B
TAG_INTEGER, TAG_BOOLEAN, TAG_ENUM = 0x21, 0x22, 0x23
TAG_TEXT, TAG_NAME, TAG_KEYWORD, TAG_URI, TAG_CHARSET, TAG_LANGUAGE, TAG_MIME = 0x41, 0x42, 0x44, 0x45, 0x47, 0x48, 0x49
EXT = {"application/postscript": "ps", "application/pdf": "pdf"}


def parse_ipp(data):
    """(version, operation, request id, [(group, name, tag, value)], document)."""
    if len(data) < 9:
        raise ValueError("short IPP request")
    version, op, rid = (data[0], data[1]), struct.unpack(">H", data[2:4])[0], struct.unpack(">I", data[4:8])[0]
    x, group, name, attrs = 8, 0, "", []
    while x < len(data):
        tag = data[x]
        x += 1
        if tag == 0x03:
            return version, op, rid, attrs, data[x:]
        if tag < 0x10:
            group = tag
            continue
        nl = struct.unpack(">H", data[x:x + 2])[0]
        name = data[x + 2:x + 2 + nl].decode("utf-8") if nl else name
        x += 2 + nl
        vl = struct.unpack(">H", data[x:x + 2])[0]
        raw = data[x + 2:x + 2 + vl]
        x += 2 + vl
        if tag in (TAG_INTEGER, TAG_ENUM) and vl == 4:
            value = struct.unpack(">i", raw)[0]
        elif tag == TAG_BOOLEAN and vl == 1:
            value = bool(raw[0])
        elif tag == 0x33 and vl == 8:
            value = list(struct.unpack(">ii", raw))
        else:
            value = raw.decode("utf-8")       # a name that isn't UTF-8 fails here, as a strict printer would
        attrs.append((group, name, tag, value))
    raise ValueError("no end-of-attributes tag")


def attr(tag, name, *values):
    out = b""
    for i, v in enumerate(values):
        if isinstance(v, bool):
            raw = bytes([1 if v else 0])
        elif isinstance(v, int):
            raw = struct.pack(">i", v)
        else:
            raw = v.encode("utf-8")
        n = name.encode() if i == 0 else b""
        out += bytes([tag]) + struct.pack(">H", len(n)) + n + struct.pack(">H", len(raw)) + raw
    return out


def response(rid, status, groups=b""):
    head = bytes([1, 1]) + struct.pack(">HI", status, rid)
    ops = b"\x01" + attr(TAG_CHARSET, "attributes-charset", "utf-8") + attr(TAG_LANGUAGE, "attributes-natural-language", "en")
    return head + ops + groups + b"\x03"


class Printer:
    def __init__(self, args):
        self.args, self.lock, self.next_id = args, threading.Lock(), 1
        self.formats = [f for f in args.formats.split(",") if f]
        os.makedirs(args.spool, exist_ok=True)

    def log(self, text):
        line = time.strftime("%Y-%m-%dT%H:%M:%S ") + text
        print(line, flush=True)
        if self.args.log:
            with open(self.args.log, "a") as f:
                f.write(line + "\n")

    def printer_attributes(self, host):
        uri = "ipp://%s/ipp/print" % host
        return (b"\x04" + attr(TAG_URI, "printer-uri-supported", uri) + attr(TAG_KEYWORD, "uri-security-supported", "none")
                + attr(TAG_NAME, "printer-name", self.args.name) + attr(TAG_TEXT, "printer-make-and-model", self.args.name)
                + attr(TAG_TEXT, "printer-location", "W157 lab stand-in (prints nothing)")
                + attr(TAG_ENUM, "printer-state", 3) + attr(TAG_KEYWORD, "printer-state-reasons", "none")
                + attr(TAG_BOOLEAN, "printer-is-accepting-jobs", True)
                + attr(TAG_MIME, "document-format-supported", *self.formats)
                + attr(TAG_MIME, "document-format-default", self.formats[0])
                + attr(TAG_BOOLEAN, "color-supported", False) + attr(TAG_KEYWORD, "sides-supported", "one-sided")
                + attr(TAG_ENUM, "operations-supported", OP_PRINT_JOB, OP_VALIDATE_JOB, OP_GET_PRINTER_ATTRIBUTES)
                + attr(TAG_CHARSET, "charset-configured", "utf-8") + attr(TAG_CHARSET, "charset-supported", "utf-8"))

    def handle(self, body, host, peer):
        version, op, rid, attrs, doc = parse_ipp(body)
        named = {n: v for g, n, t, v in attrs}
        if op == OP_GET_PRINTER_ATTRIBUTES:
            self.log("from %s: Get-Printer-Attributes" % peer)
            return response(rid, 0, self.printer_attributes(host))
        if op not in (OP_PRINT_JOB, OP_VALIDATE_JOB):
            self.log("from %s: operation 0x%04x not supported" % (peer, op))
            return response(rid, 0x0501)
        fmt = named.get("document-format", "application/octet-stream")
        if op == OP_VALIDATE_JOB:
            return response(rid, 0 if fmt in self.formats else 0x040A)
        if self.args.mode == "refuse" or fmt not in self.formats:
            self.log("from %s: Print-Job of %s refused (0x040a), %d bytes not kept" % (peer, fmt, len(doc)))
            return response(rid, 0x040A)
        with self.lock:
            job, self.next_id = self.next_id, self.next_id + 1
        base = os.path.join(self.args.spool, "job-%d" % job)
        with open(base + "." + EXT.get(fmt, "bin"), "wb") as f:
            f.write(doc)
        record = {"job-id": job, "from": peer, "ipp-version": "%d.%d" % version, "request-id": rid,
                  "attributes": [{"group": g, "name": n, "tag": "0x%02x" % t, "value": v} for g, n, t, v in attrs],
                  "document-bytes": len(doc), "document-sha256": hashlib.sha256(doc).hexdigest()}
        with open(base + ".json", "w") as f:
            json.dump(record, f, indent=1)
        self.log("from %s: Print-Job %d kept: %s, job-name %r, copies %s, %d bytes, sha256 %s" % (
            peer, job, fmt, named.get("job-name"), named.get("copies"), len(doc), record["document-sha256"]))
        groups = (b"\x02" + attr(TAG_INTEGER, "job-id", job)
                  + attr(TAG_URI, "job-uri", "ipp://%s/ipp/print/%d" % (host, job))
                  + attr(TAG_ENUM, "job-state", 9) + attr(TAG_KEYWORD, "job-state-reasons", "job-completed-successfully"))
        return response(rid, 0, groups)


def make_handler(printer):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n)
            peer = "%s:%d" % self.client_address
            if printer.args.mode == "silent":
                printer.log("from %s: %d bytes, not answered (silent)" % (peer, len(body)))
                time.sleep(120)
                return
            try:
                out = printer.handle(body, self.headers.get("Host", "printer"), peer)
            except (ValueError, UnicodeDecodeError, struct.error) as e:
                printer.log("from %s: bad request: %s" % (peer, e))
                out = response(struct.unpack(">I", body[4:8])[0] if len(body) >= 8 else 0, 0x0400)
            self.send_response(200)
            self.send_header("Content-Type", "application/ipp")
            self.send_header("Content-Length", str(len(out)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(out)
            self.close_connection = True
    return Handler


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bind", required=True)
    p.add_argument("--port", type=int, required=True)
    p.add_argument("--spool", required=True)
    p.add_argument("--formats", default="application/pdf,application/postscript")
    p.add_argument("--mode", choices=("normal", "refuse", "silent"), default="normal")
    p.add_argument("--name", default="OpenPrint stand-in")
    p.add_argument("--log")
    args = p.parse_args()
    printer = Printer(args)
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(printer))
    server.daemon_threads = True
    printer.log("listening on %s:%d, formats %s, mode %s" % (args.bind, args.port, ",".join(printer.formats), args.mode))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
