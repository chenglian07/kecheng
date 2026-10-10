"""Quick status check: DB records + backend health"""
import sqlite3
import os
import urllib.request
import json

# 1. Check database (correct path: imu_data.db)
db_path = os.path.join(os.path.dirname(__file__), "vps_backend", "imu_data.db")
print(f"=== Database Check ===")
print(f"DB path: {db_path}")
print(f"DB exists: {os.path.exists(db_path)}")
if os.path.exists(db_path):
    print(f"DB size: {os.path.getsize(db_path)} bytes")
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    
    # List tables
    c.execute("SELECT name FROM sqlite_master WHERE type='table'")
    tables = c.fetchall()
    print(f"Tables: {[t[0] for t in tables]}")
    
    for table_name in [t[0] for t in tables]:
        c.execute(f"SELECT COUNT(*) FROM [{table_name}]")
        count = c.fetchone()[0]
        print(f"\n--- {table_name}: {count} records ---")
        
        # Get column info
        c.execute(f"PRAGMA table_info([{table_name}])")
        cols = c.fetchall()
        col_names = [col[1] for col in cols]
        print(f"  Columns: {col_names}")
        
        # Show latest 5 records
        c.execute(f"SELECT * FROM [{table_name}] ORDER BY rowid DESC LIMIT 5")
        rows = c.fetchall()
        for r in rows:
            print(f"  {dict(zip(col_names, r))}")
    conn.close()

# 2. Check backend API (correct endpoint: /api/imu)
print(f"\n=== Backend API Check ===")
try:
    url = "http://127.0.0.1:8000/api/imu?limit=3"
    req = urllib.request.urlopen(url, timeout=5)
    data = json.loads(req.read().decode())
    print(f"API response: {json.dumps(data, indent=2)}")
except Exception as e:
    print(f"API error: {e}")

# 3. Check health
try:
    url = "http://127.0.0.1:8000/api/health"
    req = urllib.request.urlopen(url, timeout=5)
    data = json.loads(req.read().decode())
    print(f"\nHealth: {json.dumps(data, indent=2)}")
except Exception as e:
    print(f"Health error: {e}")