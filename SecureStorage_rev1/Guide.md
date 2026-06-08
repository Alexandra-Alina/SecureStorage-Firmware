# SecureStorage Benchmark — Operating Guide

Target board: **NUCLEO-H7S3L8** (STM32H7S3L8Hx, Cortex-M7 @ 400 MHz)  
Host machine: Windows 11, Python 3.12, STM32CubeIDE 1.13.2

---

## Prerequisites

| Tool | Path / version |
|------|---------------|
| GCC ARM toolchain | `C:/ST/STM32CubeIDE_1.13.2/...gnu-tools.../tools/bin/` |
| make | `C:/ST/STM32CubeIDE_1.13.2/.../make.win32.../tools/bin/make.exe` |
| STM32_Programmer_CLI | `C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/` |
| External loader | `.../ExternalLoader/MX25UW25645G_NUCLEO-H7S3L8.stldr` |
| Python packages | `pyserial`, `cryptography` (`pip install pyserial cryptography`) |

---

## 1. Generate TLS Certificate (first time only)

The HTTPS server needs a self-signed RSA-2048 certificate with IP SAN `192.168.0.9`.  
The firmware needs the DER-encoded version embedded as a C array.

```bash
cd SecureStorage_rev1
python server/gen_cert.py
```

Outputs:
- `server/certs/server.crt` — PEM certificate for the Python HTTPS server
- `server/certs/server.key` — RSA-2048 private key
- `Appli/Core/Inc/server_ca_cert.h` — DER cert as `SERVER_CA_CERT_DER[]` C array (used by firmware S9)

> Re-run only if the certificate expires or the server IP changes.

---

## 2. Build the Firmware

```bash
cd SecureStorage_rev1

# Build both Boot and Appli
make all

# Build Appli only (faster, after changing application code)
make appli

# Clean build
make clean
```

Outputs:
- `build/Boot/SecureStorage_rev1_Boot.elf`
- `build/Appli/SecureStorage_rev1_Appli.elf`

> After editing any `.h` file, always run `make clean appli` — header dependency tracking is not automatic.

---

## 3. Flash the Board

Connect the NUCLEO board via USB (ST-LINK). Then:

```bash
make flash
```

This flashes in two steps:
1. **Boot** → internal flash at `0x08000000`
2. **Appli** → XSPI2 NOR flash at `0x70000000` (via external loader)

The board resets automatically after each flash step.

---

## 4. Start the Servers

Open two separate terminal windows from the project root.

**HTTP telemetry server (port 5000):**
```bash
python server/server.py
```

**HTTPS server (port 8443, TLS 1.2):**
```bash
python server/server_https.py
```

Both servers share the same `server/metrics.db` SQLite database.

Routes available on both servers:

| Route | Method | Description |
|-------|--------|-------------|
| `/` | GET | Returns `SECRET_DATA_1234` (connectivity test) |
| `/metrics` | POST | Stores telemetry JSON from board |
| `/metrics` | GET | Returns all stored rows as JSON |

---

## 5. Monitor UART Output

Open a third terminal window:

```bash
python -m serial.tools.miniterm COM6 115200 --eol LF
```

> COM port may differ. Check Device Manager → Ports if COM6 is not found.

Press the **RESET button (NRST)** on the board to trigger a fresh benchmark run.

---

## 6. Network Configuration

| Device | IP address |
|--------|-----------|
| STM32 board | `192.168.0.100` (static) |
| Host PC (server) | `192.168.0.9` (static) |
| Gateway | `192.168.0.1` |

The board uses a **static IP** — no DHCP required. Connect board and PC on the same LAN segment via Ethernet.

---

## 7. Benchmark Strategies

The firmware runs strategies S1–S9 in sequence, then loops. Each cycle takes ~3–4 seconds.

| # | Name | Transport | TLS | Description |
|---|------|-----------|-----|-------------|
| S1 | ROM Static | — | — | Cert pointer into XIP NOR flash |
| S2 | Stack | — | — | Cert copied to DTCM stack |
| S3 | Heap | — | — | malloc/free around usage |
| S4 | SecureStorage | — | — | NOR → noncacheable SRAM simulation |
| S5 | AES Wrap | — | — | AES-ECB-256 encrypt/decrypt key |
| S6 | CRYP Offload | — | — | Key loaded into CRYP HW registers |
| S7 | mbedTLS sim | — | — | Simulated 9-allocation mbedTLS heap pattern |
| S8 | Real TCP | Ethernet | None | Real HTTP GET to port 5000 |
| S9 | mbedTLS HTTPS | Ethernet | TLS 1.2 | Real HTTPS GET to port 8443, RSA + AES-128-GCM |

Telemetry from each strategy is POSTed to the HTTP server (port 5000) and stored in `metrics.db`.

---

## 8. HTTPS Verbose Log Format

The HTTPS server (`server_https.py`) logs each TLS connection in detail:

```
────────────────────────────────────────────────────────────
  [10:52:14.231]  NEW TLS CONNECTION
  Client   : 192.168.0.100:52501
  Protocol : TLSv1.2
  Cipher   : AES128-GCM-SHA256  (128 bits)
────────────────────────────────────────────────────────────
  [10:52:14.245]  GET /
             -> 200  (14.1 ms)
  [10:52:14.246]  CONNECTION CLOSED  (15.3 ms total)
────────────────────────────────────────────────────────────
```

---

## 9. Inspect the Database

```bash
python -c "
import sqlite3, json
conn = sqlite3.connect('server/metrics.db')
rows = conn.execute('SELECT * FROM metrics ORDER BY id DESC LIMIT 20').fetchall()
for r in rows: print(r)
conn.close()
"
```

Or dump as JSON via the HTTP server:
```bash
curl http://192.168.0.9:5000/metrics
```

---

## 10. TLS Certificate Details

| Field | Value |
|-------|-------|
| Type | X.509 RSA-2048, self-signed |
| CN | `SecureStorage-Bench-Server` |
| IP SAN | `192.168.0.9` |
| Valid | 2026–2036 |
| Cipher suite (S9) | `TLS-RSA-WITH-AES-128-GCM-SHA256` |
| Verification | `VERIFY_OPTIONAL` (self-signed) |
| Embedded in firmware | `Appli/Core/Inc/server_ca_cert.h` |

---

## 11. Key File Locations

| File | Purpose |
|------|---------|
| `Appli/Core/Src/cert_strategies.c` | All 9 strategy implementations |
| `Appli/Core/Src/tls_client.c` | mbedTLS + LwIP raw TCP client (S9) |
| `Appli/Core/Inc/mbedtls_config.h` | Minimal mbedTLS 3.6 config for STM32H7 |
| `Appli/Core/Inc/server_ca_cert.h` | Embedded DER certificate for TLS verification |
| `Appli/Core/Inc/cert_test_data.h` | Synthetic 272-byte DER cert used by S1–S8 |
| `server/server.py` | HTTP telemetry server (port 5000) |
| `server/server_https.py` | HTTPS server with verbose TLS logging (port 8443) |
| `server/gen_cert.py` | Certificate generator |
| `server/metrics.db` | SQLite database (auto-created) |
| `Middlewares/Third_Party/mbedtls/` | mbedTLS 3.6.6 source |
