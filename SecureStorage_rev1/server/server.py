#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')
"""
server.py — Bench HTTP server for NUCLEO-H7S3L8 telemetry.

Usage:
    python server/server.py          # listens on port 5000
    python server/server.py 9000     # listens on port 9000

Routes:
    GET  /          -> 200  "SECRET_DATA_1234"   (S8 connectivity test)
    POST /metrics   -> 201  {"status":"ok","id":<n>}  store telemetry in SQLite3
    GET  /metrics   -> 200  JSON array of all stored rows (for inspection)
    *               -> 404

Telemetry JSON body expected for POST /metrics:
    {
      "timestamp":          <int ms since board boot>,
      "cpu_usage":          <float % of 1 second>,
      "memory_usage":       <int bytes peak heap>,
      "packets_in":         <int frames received during strategy>,
      "packets_out":        <int frames transmitted during strategy>,
      "power_consumption":  <float, 0.0 placeholder>,
      "security_scenario":  "<string name>"
    }

Database: server/metrics.db (SQLite3, created automatically).
"""

import http.server
import json
import os
import sqlite3
import sys
import threading
import datetime

PORT     = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
DB_PATH  = os.path.join(os.path.dirname(__file__), "metrics.db")
DB_LOCK      = threading.Lock()
_run_tracker = {}   # scenario -> last row_id seen; used to detect new benchmark runs

SCHEMA = """
CREATE TABLE IF NOT EXISTS metrics (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    received_at        TEXT    DEFAULT (datetime('now')),
    timestamp_ms       INTEGER,
    cpu_usage          REAL,
    memory_usage       INTEGER,
    packets_in         INTEGER,
    packets_out        INTEGER,
    power_consumption  REAL,
    security_scenario  TEXT
);
"""

REQUIRED_FIELDS = {
    "timestamp", "cpu_usage", "memory_usage",
    "packets_in", "packets_out", "power_consumption", "security_scenario"
}


def _open_db():
    conn = sqlite3.connect(DB_PATH)
    conn.execute(SCHEMA)
    conn.commit()
    return conn


class Handler(http.server.BaseHTTPRequestHandler):

    # ------------------------------------------------------------------
    def log_message(self, fmt, *args):
        print(f"[server] {self.address_string()} {fmt % args}", flush=True)

    # ------------------------------------------------------------------
    # GET /
    # ------------------------------------------------------------------
    def _handle_get_root(self):
        body = b"SECRET_DATA_1234"
        self.send_response(200)
        self.send_header("Content-Type",   "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection",     "close")
        self.end_headers()
        self.wfile.write(body)

    # ------------------------------------------------------------------
    # GET /metrics  — return all rows as JSON array
    # ------------------------------------------------------------------
    def _handle_get_metrics(self):
        with DB_LOCK:
            conn = _open_db()
            rows = conn.execute(
                "SELECT id, received_at, timestamp_ms, cpu_usage, memory_usage,"
                "       packets_in, packets_out, power_consumption, security_scenario"
                "  FROM metrics ORDER BY id"
            ).fetchall()
            conn.close()

        cols = ["id", "received_at", "timestamp_ms", "cpu_usage", "memory_usage",
                "packets_in", "packets_out", "power_consumption", "security_scenario"]
        data = [dict(zip(cols, row)) for row in rows]
        body = json.dumps(data, indent=2).encode()

        self.send_response(200)
        self.send_header("Content-Type",   "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection",     "close")
        self.end_headers()
        self.wfile.write(body)

    # ------------------------------------------------------------------
    # POST /metrics  — store one telemetry record
    # ------------------------------------------------------------------
    def _handle_post_metrics(self):
        length = int(self.headers.get("Content-Length", 0))
        raw    = self.rfile.read(length)

        try:
            rec = json.loads(raw.decode("utf-8"))
        except Exception as exc:
            self._send_json(400, {"error": f"JSON parse error: {exc}"}); return

        missing = REQUIRED_FIELDS - set(rec.keys())
        if missing:
            self._send_json(400, {"error": f"missing fields: {sorted(missing)}"}); return

        with DB_LOCK:
            conn = _open_db()
            cur  = conn.execute(
                "INSERT INTO metrics"
                "(timestamp_ms, cpu_usage, memory_usage,"
                " packets_in, packets_out, power_consumption, security_scenario)"
                " VALUES (?,?,?,?,?,?,?)",
                (
                    int(rec["timestamp"]),
                    float(rec["cpu_usage"]),
                    int(rec["memory_usage"]),
                    int(rec["packets_in"]),
                    int(rec["packets_out"]),
                    float(rec["power_consumption"]),
                    str(rec["security_scenario"]),
                )
            )
            conn.commit()
            row_id = cur.lastrowid
            conn.close()

        scenario = rec['security_scenario']
        # Detect start of a new benchmark run (S1_ROM_Static reappears)
        if scenario == "S1_ROM_Static" and _run_tracker:
            run_num = (max(_run_tracker.values()) // 8) + 1
            print(f"\n{'='*60}", flush=True)
            print(f"  NEW BENCHMARK RUN  (run #{run_num}  —  {datetime.datetime.now().strftime('%H:%M:%S')})", flush=True)
            print(f"{'='*60}", flush=True)
        _run_tracker[scenario] = row_id

        ts = datetime.datetime.now().strftime("%H:%M:%S")
        print(f"\n[recv] ──── {ts}  id={row_id} ────────────────────────────────", flush=True)
        print(f"[json]  scenario        : {scenario}", flush=True)
        print(f"[json]  timestamp_ms    : {rec['timestamp']} ms (board uptime)", flush=True)
        print(f"[json]  cpu_usage       : {rec['cpu_usage']:.6f} %", flush=True)
        print(f"[json]  memory_usage    : {rec['memory_usage']} B (peak heap)", flush=True)
        print(f"[json]  packets_in      : {rec['packets_in']}", flush=True)
        print(f"[json]  packets_out     : {rec['packets_out']}", flush=True)
        print(f"[json]  power           : {rec['power_consumption']}", flush=True)
        print(f"[db]    stored → row #{row_id}", flush=True)

        self._send_json(201, {"status": "ok", "id": row_id})

    # ------------------------------------------------------------------
    # Router
    # ------------------------------------------------------------------
    def do_GET(self):
        if self.path == "/":
            self._handle_get_root()
        elif self.path.startswith("/metrics"):
            self._handle_get_metrics()
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        if self.path == "/metrics":
            self._handle_post_metrics()
        else:
            self._send_json(404, {"error": "not found"})

    # ------------------------------------------------------------------
    def _send_json(self, code, obj):
        body = json.dumps(obj).encode()
        try:
            self.send_response(code)
            self.send_header("Content-Type",   "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection",     "close")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            pass  # board closed socket before we finished flushing — data already stored

    def log_error(self, fmt, *args):
        # Silence the default traceback dumps for connection-reset errors
        msg = fmt % args
        if "ConnectionAbortedError" in msg or "ConnectionResetError" in msg:
            return
        print(f"[server-err] {msg}", flush=True)


if __name__ == "__main__":
    # Create DB + table on startup so the first request is instant
    with DB_LOCK:
        conn = _open_db(); conn.close()

    print(f"{'='*60}", flush=True)
    print(f"  SecureStorage Telemetry Server", flush=True)
    print(f"  Listening on  0.0.0.0:{PORT}", flush=True)
    print(f"  Database      {DB_PATH}", flush=True)
    print(f"  Started       {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}", flush=True)
    print(f"{'='*60}", flush=True)
    print(f"[server] GET /           -> SECRET_DATA_1234  (S8 connectivity)", flush=True)
    print(f"[server] POST /metrics   -> store telemetry JSON, print received fields", flush=True)
    print(f"[server] GET  /metrics   -> dump all rows as JSON", flush=True)
    print(f"[server] Waiting for board (192.168.0.100) — Ctrl+C to stop.", flush=True)
    print(f"", flush=True)

    srv = http.server.HTTPServer(("", PORT), Handler)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[server] Stopped.", flush=True)
