import sqlite3
conn = sqlite3.connect(r'd:\shangke\zhou\week1\vps_backend\imu_data.db')
c = conn.cursor()
c.execute('SELECT * FROM imu_records ORDER BY id DESC LIMIT 10')
rows = c.fetchall()
with open(r'd:\shangke\zhou\week1\db_result.txt', 'w') as f:
    f.write("=== Latest 10 records ===\n")
    for r in rows:
        f.write(f"  ID={r[0]:3d} | dev={r[1]:45s} | ax={r[3]:8.1f} ay={r[4]:8.1f} az={r[5]:8.1f} mg | created={r[6]}\n")
    c.execute('SELECT COUNT(*) FROM imu_records')
    f.write(f"\nTotal records: {c.fetchone()[0]}\n")
conn.close()
print("DONE")
