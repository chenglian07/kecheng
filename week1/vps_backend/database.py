"""
database.py - SQLite 数据库管理

表结构：imu_data
  id         INTEGER PRIMARY KEY AUTOINCREMENT
  device_id  TEXT    NOT NULL      -- 设备 ID（如 group_01）
  timestamp  INTEGER NOT NULL      -- Unix 时间戳（秒）
  ax_mg      REAL    NOT NULL      -- X 轴加速度（mg）
  ay_mg      REAL    NOT NULL      -- Y 轴加速度（mg）
  az_mg      REAL    NOT NULL      -- Z 轴加速度（mg）
  raw_x      INTEGER NOT NULL      -- X 轴原始值
  raw_y      INTEGER NOT NULL      -- Y 轴原始值
  raw_z      INTEGER NOT NULL      -- Z 轴原始值
  chip_id    INTEGER NOT NULL      -- 芯片 ID
  created_at TEXT    DEFAULT (datetime('now'))  -- 服务端接收时间
"""

import sqlite3
import os
from contextlib import contextmanager

DB_PATH = os.path.join(os.path.dirname(__file__), "imu_data.db")


def get_connection():
    """获取数据库连接"""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")  # 并发写入优化
    return conn


@contextmanager
def get_db():
    """上下文管理器：自动提交/回滚"""
    conn = get_connection()
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def init_db():
    """初始化数据库表"""
    with get_db() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS imu_data (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id  TEXT    NOT NULL,
                timestamp  INTEGER NOT NULL,
                ax_mg      REAL    NOT NULL,
                ay_mg      REAL    NOT NULL,
                az_mg      REAL    NOT NULL,
                raw_x      INTEGER NOT NULL,
                raw_y      INTEGER NOT NULL,
                raw_z      INTEGER NOT NULL,
                chip_id    INTEGER NOT NULL,
                created_at TEXT    DEFAULT (datetime('now', 'localtime'))
            )
        """)
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_device_ts
            ON imu_data(device_id, timestamp DESC)
        """)
    print(f"[DB] 数据库初始化完成: {DB_PATH}")


def insert_imu_record(device_id: str, timestamp: int,
                      ax_mg: float, ay_mg: float, az_mg: float,
                      raw_x: int, raw_y: int, raw_z: int,
                      chip_id: int) -> int:
    """插入一条 IMU 记录，返回行 ID"""
    with get_db() as conn:
        cur = conn.execute("""
            INSERT INTO imu_data
                (device_id, timestamp, ax_mg, ay_mg, az_mg,
                 raw_x, raw_y, raw_z, chip_id)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (device_id, timestamp, ax_mg, ay_mg, az_mg,
              raw_x, raw_y, raw_z, chip_id))
        return cur.lastrowid


def query_latest(device_id: str = None, limit: int = 50) -> list[dict]:
    """查询最新的 IMU 记录"""
    with get_db() as conn:
        if device_id:
            rows = conn.execute("""
                SELECT * FROM imu_data
                WHERE device_id = ?
                ORDER BY timestamp DESC
                LIMIT ?
            """, (device_id, limit)).fetchall()
        else:
            rows = conn.execute("""
                SELECT * FROM imu_data
                ORDER BY timestamp DESC
                LIMIT ?
            """, (limit,)).fetchall()
        return [dict(r) for r in rows]


def query_devices() -> list[str]:
    """查询所有已注册的设备 ID"""
    with get_db() as conn:
        rows = conn.execute("""
            SELECT DISTINCT device_id FROM imu_data
            ORDER BY device_id
        """).fetchall()
        return [r["device_id"] for r in rows]