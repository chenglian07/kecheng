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
from datetime import datetime, timedelta

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
        # ---- 第二周：任务表 ----
        conn.execute("""
            CREATE TABLE IF NOT EXISTS tasks (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                request_id      TEXT    NOT NULL UNIQUE,
                device_id       TEXT    NOT NULL,
                command         TEXT    NOT NULL DEFAULT 'collect_once',
                status          TEXT    NOT NULL DEFAULT 'submitted',
                imu_ax_mg       REAL,
                imu_ay_mg       REAL,
                imu_az_mg       REAL,
                imu_raw_x       INTEGER,
                imu_raw_y       INTEGER,
                imu_raw_z       INTEGER,
                imu_chip_id     INTEGER,
                imu_timestamp   INTEGER,
                created_at      TEXT    DEFAULT (datetime('now','localtime')),
                received_at     TEXT,
                completed_at    TEXT,
                error_message   TEXT
            )
        """)
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_tasks_status
            ON tasks(status)
        """)
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_tasks_device
            ON tasks(device_id, created_at DESC)
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


# ============ 第二周：任务相关函数 ============

def insert_task(request_id: str, device_id: str,
                command: str = "collect_once") -> int:
    """插入一条新任务，返回行 ID"""
    with get_db() as conn:
        cur = conn.execute("""
            INSERT INTO tasks (request_id, device_id, command, status)
            VALUES (?, ?, ?, 'submitted')
        """, (request_id, device_id, command))
        return cur.lastrowid


def get_task_by_request_id(request_id: str) -> dict | None:
    """根据 request_id 查询任务"""
    with get_db() as conn:
        row = conn.execute(
            "SELECT * FROM tasks WHERE request_id = ?", (request_id,)
        ).fetchone()
        return dict(row) if row else None


def get_pending_task(device_id: str) -> dict | None:
    """获取指定设备最早的 submitted 任务，并将其状态更新为 received。
    返回任务字典；如果没有待执行任务则返回 None。"""
    with get_db() as conn:
        row = conn.execute("""
            SELECT * FROM tasks
            WHERE device_id = ? AND status = 'submitted'
            ORDER BY created_at ASC
            LIMIT 1
        """, (device_id,)).fetchone()
        if row:
            now = datetime.now().isoformat(sep=' ', timespec='seconds')
            conn.execute("""
                UPDATE tasks SET status = 'received', received_at = ?
                WHERE request_id = ?
            """, (now, row["request_id"]))
            return dict(row)
    return None


def update_task_complete(request_id: str,
                         ax_mg: float, ay_mg: float, az_mg: float,
                         raw_x: int, raw_y: int, raw_z: int,
                         chip_id: int, timestamp: int) -> bool:
    """将任务标记为 completed，并保存 IMU 数据"""
    now = datetime.now().isoformat(sep=' ', timespec='seconds')
    with get_db() as conn:
        cur = conn.execute("""
            UPDATE tasks
            SET status = 'completed', completed_at = ?,
                imu_ax_mg = ?, imu_ay_mg = ?, imu_az_mg = ?,
                imu_raw_x = ?, imu_raw_y = ?, imu_raw_z = ?,
                imu_chip_id = ?, imu_timestamp = ?
            WHERE request_id = ?
              AND status IN ('submitted', 'received')
        """, (now, ax_mg, ay_mg, az_mg,
              raw_x, raw_y, raw_z, chip_id, timestamp, request_id))
        return cur.rowcount > 0


def update_task_failed(request_id: str, error: str) -> bool:
    """将任务标记为 failed"""
    now = datetime.now().isoformat(sep=' ', timespec='seconds')
    with get_db() as conn:
        cur = conn.execute("""
            UPDATE tasks
            SET status = 'failed', completed_at = ?, error_message = ?
            WHERE request_id = ?
              AND status IN ('submitted', 'received')
        """, (now, error, request_id))
        return cur.rowcount > 0


def mark_timeout_tasks(timeout_seconds: int = 30) -> int:
    """将超时任务标记为 timeout，返回受影响行数"""
    cutoff = (datetime.now() - timedelta(seconds=timeout_seconds)
              ).isoformat(sep=' ', timespec='seconds')
    with get_db() as conn:
        cur = conn.execute("""
            UPDATE tasks
            SET status = 'timeout', error_message = ?
            WHERE status IN ('submitted', 'received')
              AND created_at < ?
        """, (f"任务超时（>{timeout_seconds}秒未响应）", cutoff))
        return cur.rowcount