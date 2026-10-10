import sqlite3
conn = sqlite3.connect(r'd:\shangke\zhou\week1\vps_backend\imu_data.db')
c = conn.cursor()
c.execute("SELECT COUNT(*) FROM imu_data")
total = c.fetchone()[0]
c.execute("SELECT * FROM imu_data ORDER BY id DESC LIMIT 5")
rows = c.fetchall()
with open(r'd:\shangke\zhou\week1\db_final.txt', 'w') as f:
    f.write(f"Total records: {total}\n\nLatest 5:\n")
    for r in rows:
        f.write(f"  ID={r[0]} dev={r[1]} ts={r[2]} ax={r[3]} ay={r[4]} az={r[5]} raw=({r[6]},{r[7]},{r[8]}) chip={r[9]} created={r[10]}\n")
conn.close()

