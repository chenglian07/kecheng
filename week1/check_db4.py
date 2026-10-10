import sqlite3, time
time.sleep(15)
conn = sqlite3.connect(r'd:\shangke\zhou\week1\vps_backend\imu_data.db')
c = conn.cursor()
c.execute("SELECT * FROM imu_data ORDER BY id DESC LIMIT 15")
rows = c.fetchall()
with open(r'd:\shangke\zhou\week1\db_result2.txt', 'w') as f:
    for r in rows:
        f.write(f"ID={r[0]:3d} | dev={r[1]:45s} | ax={r[3]:8.1f} ay={r[4]:8.1f} az={r[5]:8.1f} | {r[10]}\n")
    c.execute("SELECT COUNT(*) FROM imu_data")
    total = c.fetchone()[0]
    f.write(f"\nTotal: {total}\n")
    c.execute("SELECT COUNT(*) FROM imu_data WHERE device_id LIKE ? OR device_id LIKE ?", ('%esp32%', '%g02%'))
    f.write(f"ESP32 records: {c.fetchone()[0]}\n")
conn.close()
print("DONE")

