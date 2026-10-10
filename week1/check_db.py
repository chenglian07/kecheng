"""检查数据库中的设备数据"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'vps_backend'))
from database import query_latest, query_devices

print("=" * 60)
print("数据库设备检查")
print("=" * 60)

# 查询所有设备
devices = query_devices()
print(f"\n已注册设备数量: {len(devices)}")
for d in devices:
    print(f"  - {d}")

# 查询最新数据
print("\n最新 10 条数据记录:")
print("-" * 60)
records = query_latest(limit=10)
for r in records:
    print(f"ID={r['id']:3d} | 设备: {r['device_id']:20s} | "
          f"时间戳: {r['timestamp']} | "
          f"AX={r['ax_mg']:7.1f} AY={r['ay_mg']:7.1f} AZ={r['az_mg']:7.1f} mg | "
          f"创建: {r['created_at']}")

print("\n" + "=" * 60)
