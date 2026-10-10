"""插入模拟测试数据到 week1 数据库"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'vps_backend'))
from database import init_db, insert_imu_record

init_db()

base_ts = int(time.time()) - 60  # 从 1 分钟前开始

records = [
    ("group_01", base_ts,      43.9,  -109.4, 7929.7,  45,  -112, 8120, 179),
    ("group_01", base_ts + 5,  52.1,  -98.3,  7915.2,  53,  -101, 8105, 179),
    ("group_01", base_ts + 10, 61.4,  -87.6,  7890.5,  63,  -90,  8080, 179),
    ("group_02", base_ts + 15, -30.2, 201.5,  8100.3,  -31, 206,  8295, 179),
    ("group_02", base_ts + 20, -28.7, 195.8,  8095.1,  -29, 201,  8290, 179),
    ("group_01", base_ts + 25, 55.0,  -105.2, 7920.0,  56,  -108, 8110, 179),
    ("group_01", base_ts + 30, 48.3,  -112.7, 7935.8,  50,  -115, 8126, 179),
    ("group_02", base_ts + 35, -25.1, 210.3,  8110.6,  -26, 215,  8305, 179),
]

for r in records:
    rid = insert_imu_record(*r)
    print(f"  插入 ID={rid}: device={r[0]}, ts={r[1]}, ax={r[2]} mg, ay={r[3]} mg, az={r[4]} mg")

print(f"\n共插入 {len(records)} 条模拟数据")
print("打开浏览器访问 http://localhost:8000/ 查看页面效果")
