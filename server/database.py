"""
SQLite 数据库管理模块
"""

import sqlite3
import os
from datetime import datetime, timezone

DB_PATH = os.path.join(os.path.dirname(__file__), "sensor_data.db")


def get_connection() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


def init_db():
    """初始化数据库表结构"""
    conn = get_connection()
    conn.execute("""
        CREATE TABLE IF NOT EXISTS sensor_data (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id   TEXT NOT NULL,
            sensor      TEXT NOT NULL DEFAULT 'qma7981',
            ts_device   REAL NOT NULL,
            ts_server   TEXT NOT NULL,
            ax          REAL NOT NULL,
            ay          REAL NOT NULL,
            az          REAL NOT NULL,
            unit        TEXT NOT NULL DEFAULT 'g',
            raw_x       INTEGER NOT NULL DEFAULT 0,
            raw_y       INTEGER NOT NULL DEFAULT 0,
            raw_z       INTEGER NOT NULL DEFAULT 0,
            status      TEXT NOT NULL DEFAULT 'ok'
        )
    """)
    conn.execute("""
        CREATE INDEX IF NOT EXISTS idx_device_ts
        ON sensor_data(device_id, ts_device DESC)
    """)
    conn.commit()
    conn.close()


def insert_sensor_record(
    device_id: str,
    sensor: str,
    ts_device: float,
    ax: float,
    ay: float,
    az: float,
    unit: str,
    raw_x: int = 0,
    raw_y: int = 0,
    raw_z: int = 0,
    status: str = "ok",
) -> int:
    """插入一条传感器记录，返回自增 ID"""
    conn = get_connection()
    ts_server = datetime.now(timezone.utc).isoformat()
    cursor = conn.execute(
        """INSERT INTO sensor_data
           (device_id, sensor, ts_device, ts_server, ax, ay, az, unit, raw_x, raw_y, raw_z, status)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (device_id, sensor, ts_device, ts_server, ax, ay, az, unit, raw_x, raw_y, raw_z, status),
    )
    row_id = cursor.lastrowid
    conn.commit()
    conn.close()
    return row_id


def query_recent(device_id: str | None = None, limit: int = 20) -> list[dict]:
    """查询最近的传感器记录"""
    conn = get_connection()
    if device_id:
        rows = conn.execute(
            """SELECT * FROM sensor_data
               WHERE device_id = ?
               ORDER BY ts_device DESC
               LIMIT ?""",
            (device_id, limit),
        ).fetchall()
    else:
        rows = conn.execute(
            """SELECT * FROM sensor_data
               ORDER BY ts_device DESC
               LIMIT ?""",
            (limit,),
        ).fetchall()
    conn.close()
    return [dict(r) for r in rows]