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
    # Week 2: 添加 request_id 列（若不存在）
    try:
        conn.execute("ALTER TABLE sensor_data ADD COLUMN request_id TEXT")
    except sqlite3.OperationalError:
        pass  # 列已存在
    conn.execute("""
        CREATE TABLE IF NOT EXISTS commands (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            request_id      TEXT NOT NULL UNIQUE,
            device_id       TEXT NOT NULL,
            sensor          TEXT NOT NULL DEFAULT 'qma7981',
            command_type    TEXT NOT NULL DEFAULT 'collect_once',
            params          TEXT,
            status          TEXT NOT NULL DEFAULT 'pending',
            created_at      TEXT NOT NULL,
            received_at     TEXT,
            completed_at    TEXT,
            result_id       INTEGER,
            ts_expire       REAL,
            FOREIGN KEY (result_id) REFERENCES sensor_data(id)
        )
    """)
    conn.execute("""
        CREATE INDEX IF NOT EXISTS idx_cmd_device_status
        ON commands(device_id, status)
    """)
    conn.execute("""
        CREATE INDEX IF NOT EXISTS idx_cmd_request_id
        ON commands(request_id)
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
    request_id: str | None = None,
) -> int:
    """插入一条传感器记录，返回自增 ID"""
    conn = get_connection()
    ts_server = datetime.now(timezone.utc).isoformat()
    cursor = conn.execute(
        """INSERT INTO sensor_data
           (device_id, sensor, ts_device, ts_server, ax, ay, az, unit, raw_x, raw_y, raw_z, status, request_id)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (device_id, sensor, ts_device, ts_server, ax, ay, az, unit, raw_x, raw_y, raw_z, status, request_id),
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


# ========== Commands (Week 2) ==========

import uuid


def create_command(
    device_id: str,
    command_type: str = "collect_once",
    sensor: str = "qma7981",
    params: str | None = None,
    timeout_sec: int = 30,
) -> dict:
    """创建一条命令，返回完整记录"""
    request_id = str(uuid.uuid4())
    now = datetime.now(timezone.utc).isoformat()
    ts_expire = None
    if timeout_sec > 0:
        from time import time
        ts_expire = time() + timeout_sec

    conn = get_connection()
    cursor = conn.execute(
        """INSERT INTO commands
           (request_id, device_id, sensor, command_type, params, status, created_at, ts_expire)
           VALUES (?, ?, ?, ?, ?, 'pending', ?, ?)""",
        (request_id, device_id, sensor, command_type, params, now, ts_expire),
    )
    conn.commit()
    row = conn.execute(
        "SELECT * FROM commands WHERE id = ?", (cursor.lastrowid,)
    ).fetchone()
    conn.close()
    return dict(row)


def get_pending_commands(device_id: str, limit: int = 5) -> list[dict]:
    """获取设备待处理的命令"""
    now_str = datetime.now(timezone.utc).isoformat()
    conn = get_connection()
    rows = conn.execute(
        """SELECT * FROM commands
           WHERE device_id = ? AND status = 'pending'
           ORDER BY created_at ASC
           LIMIT ?""",
        (device_id, limit),
    ).fetchall()
    conn.close()
    # 检查是否超时
    results = []
    for r in rows:
        r = dict(r)
        if r["ts_expire"]:
            from time import time
            if time() > r["ts_expire"]:
                update_command_status(r["request_id"], "timeout")
                continue
        results.append(r)
    return results


def update_command_status(
    request_id: str,
    status: str,
    result_id: int | None = None,
) -> dict | None:
    """更新命令状态，返回更新后的记录"""
    now = datetime.now(timezone.utc).isoformat()
    conn = get_connection()

    if status == "received":
        conn.execute(
            "UPDATE commands SET status = ?, received_at = ? WHERE request_id = ?",
            (status, now, request_id),
        )
    elif status == "completed":
        if result_id:
            conn.execute(
                "UPDATE commands SET status = ?, completed_at = ?, result_id = ? WHERE request_id = ?",
                (status, now, result_id, request_id),
            )
        else:
            conn.execute(
                "UPDATE commands SET status = ?, completed_at = ? WHERE request_id = ?",
                (status, now, request_id),
            )
    else:
        conn.execute(
            "UPDATE commands SET status = ? WHERE request_id = ?",
            (status, request_id),
        )
    conn.commit()
    row = conn.execute(
        "SELECT * FROM commands WHERE request_id = ?", (request_id,)
    ).fetchone()
    conn.close()
    return dict(row) if row else None


def get_command(request_id: str) -> dict | None:
    """查询单条命令"""
    conn = get_connection()
    row = conn.execute(
        "SELECT * FROM commands WHERE request_id = ?", (request_id,)
    ).fetchone()
    conn.close()
    return dict(row) if row else None


def expire_old_commands(device_id: str | None = None, max_age_sec: int = 60):
    """将超时的 pending 命令标记为 timeout"""
    from time import time
    now = time()
    conn = get_connection()
    if device_id:
        conn.execute(
            """UPDATE commands SET status = 'timeout'
               WHERE device_id = ? AND status = 'pending' AND ts_expire IS NOT NULL AND ts_expire < ?""",
            (device_id, now),
        )
    else:
        conn.execute(
            """UPDATE commands SET status = 'timeout'
               WHERE status = 'pending' AND ts_expire IS NOT NULL AND ts_expire < ?""",
            (now,),
        )
    conn.commit()
    conn.close()