import sqlite3, time
time.sleep(8)
conn = sqlite3.connect(r'd:\shangke\zhou\week1\vps_backend\imu_data.db')
c = conn.cursor()
c.execute('SELECT * FROM imu_records ORDER BY id DESC LIMIT 10')
rows = c.fetchall()
print("=== Latest 10 records ===")
for r in rows:
    print(f"  ID={r[0]:3d} | dev={r[1]:40s} | ax={r[3]:8.1f} ay={r[4]:8.1f} az={r[5]:8.1f} mg | created={r[6]}")
c.execute('SELECT COUNT(*) FROM imu_records')
print(f"\nTotal records: {c.fetchone()[0]}")
conn.close()
