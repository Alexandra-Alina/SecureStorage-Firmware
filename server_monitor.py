import urllib.request, json, time

URL = "http://136.113.205.153:5000/metrics"
seen = set()

print("=== Server Monitor (136.113.205.153:5000) ===")
print("Polling every 5 seconds — Ctrl+C to stop")
print()

while True:
    try:
        with urllib.request.urlopen(URL, timeout=6) as r:
            rows = json.loads(r.read())
            new = [row for row in rows if (row['host_id'], row['timestamp']) not in seen]
            for row in new:
                seen.add((row['host_id'], row['timestamp']))
                m = row.get('metrics', {})
                net_out = m.get('network_out_bytes', 0)
                cpu     = m.get('cpu_load_percent', 0.0)
                print(f"[{row['timestamp']}] {row['host_id']:<25}"
                      f"  cpu={cpu:.1f}%"
                      f"  net_out={net_out}B", flush=True)
    except Exception as e:
        print(f"  connection error: {e}", flush=True)
    time.sleep(5)
