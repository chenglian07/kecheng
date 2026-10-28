"""
传感器数据接收服务 (FastAPI)

端点：
  POST /api/ingest                   — 接收传感器 JSON 数据
  GET  /api/data                     — 查询最近记录
  POST /api/commands                 — 创建远程采集命令
  GET  /api/commands/{device_id}/pending  — 设备轮询待处理命令
  PUT  /api/commands/{request_id}    — 设备更新命令状态
  GET  /api/commands/{request_id}    — 查询命令状态
  GET  /                             — Web 展示页面
"""

import os
import sys
from time import time

# 确保可以 import database.py
sys.path.insert(0, os.path.dirname(__file__))

from fastapi import FastAPI, Query, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from database import (
    init_db, insert_sensor_record, query_recent,
    create_command, get_pending_commands, update_command_status, get_command,
    expire_old_commands, query_recent_commands,
)

app = FastAPI(title="KECHENG Sensor API", version="0.2.0")

# Web 静态文件
static_dir = os.path.join(os.path.dirname(__file__), "static")
if os.path.isdir(static_dir):
    app.mount("/static", StaticFiles(directory=static_dir), name="static")


# ---------- Pydantic models ----------

class SensorPayload(BaseModel):
    device_id: str
    sensor: str = "qma7981"
    ts_device: float
    ax: float
    ay: float
    az: float
    unit: str = "g"
    raw: list[int] = Field(default_factory=lambda: [0, 0, 0])
    status: str = "ok"
    request_id: str | None = None  # Week 2: 关联远程采集命令


class CreateCommandPayload(BaseModel):
    device_id: str
    command_type: str = "collect_once"
    sensor: str = "qma7981"
    params: str | None = None
    timeout_sec: int = 60


class UpdateCommandPayload(BaseModel):
    status: str  # 'received', 'completed'
    result_id: int | None = None


# ---------- Lifecycle ----------

@app.on_event("startup")
def on_startup():
    init_db()


# ---------- Server time API ----------

@app.get("/api/server_time")
def get_server_time():
    """返回服务器当前 Unix 时间戳（用于前端倒计时同步）"""
    return {"code": 0, "server_time": time()}


# ---------- Sensor data API ----------

@app.post("/api/imu")
async def ingest_imu_compat(request: Request):
    """兼容 Week 1 固件 /api/imu 路径（接受旧 JSON 格式并转换入库）

    Week 1 JSON: device_id, timestamp, ax_mg, ay_mg, az_mg, raw_x, raw_y, raw_z, chip_id
    Week 2 JSON: device_id, ts_device, ax, ay, az, raw, ...  (与 /api/ingest 相同)
    """
    body = await request.json()

    # 检测 Week 1 旧格式（含 ax_mg 字段）
    if "ax_mg" in body:
        raw = [
            int(body.get("raw_x", 0)),
            int(body.get("raw_y", 0)),
            int(body.get("raw_z", 0)),
        ]
        payload = SensorPayload(
            device_id=str(body.get("device_id", "unknown")),
            sensor="qma7981",
            ts_device=float(body.get("timestamp", 0)),
            ax=float(body.get("ax_mg", 0)) / 1000.0,
            ay=float(body.get("ay_mg", 0)) / 1000.0,
            az=float(body.get("az_mg", 0)) / 1000.0,
            unit="g",
            raw=raw,
            status="ok",
        )
    else:
        # Week 2 格式，直接用 SensorPayload 校验
        payload = SensorPayload(**body)

    return ingest_data(payload)


@app.post("/api/ingest")
def ingest_data(payload: SensorPayload):
    """接收传感器数据并存入数据库"""
    raw = payload.raw if payload.raw else [0, 0, 0]
    row_id = insert_sensor_record(
        device_id=payload.device_id,
        sensor=payload.sensor,
        ts_device=payload.ts_device,
        ax=payload.ax,
        ay=payload.ay,
        az=payload.az,
        unit=payload.unit,
        raw_x=raw[0] if len(raw) > 0 else 0,
        raw_y=raw[1] if len(raw) > 1 else 0,
        raw_z=raw[2] if len(raw) > 2 else 0,
        status=payload.status,
        request_id=payload.request_id,
    )

    # 如果有关联的 request_id，更新命令状态为 completed
    if payload.request_id:
        cmd = get_command(payload.request_id)
        if cmd and cmd["status"] in ("pending", "received"):
            update_command_status(payload.request_id, "completed", result_id=row_id)

    return {"code": 0, "message": "ok", "id": row_id}


@app.get("/api/data")
def get_data(
    device_id: str | None = Query(None),
    limit: int = Query(default=20, le=100),
):
    """查询最近的传感器数据"""
    records = query_recent(device_id, limit)
    return {"code": 0, "data": records}


# ---------- Remote command API (Week 2) ----------

@app.post("/api/commands")
def create_collect_command(payload: CreateCommandPayload):
    """创建远程采集命令，返回命令记录"""
    cmd = create_command(
        device_id=payload.device_id,
        command_type=payload.command_type,
        sensor=payload.sensor,
        params=payload.params,
        timeout_sec=payload.timeout_sec,
    )
    return {"code": 0, "message": "command created", "data": cmd}


# ⚠️ 固定路径必须放在 {path_param} 前面！
@app.get("/api/commands/history")
def command_history(
    device_id: str | None = Query(None),
    limit: int = Query(default=20, le=50),
):
    """查询最近的命令历史（Web 面板展示）"""
    expire_old_commands(device_id)
    commands = query_recent_commands(device_id, limit)
    return {"code": 0, "data": commands}


@app.get("/api/commands/{device_id}/pending")
def poll_pending_commands(device_id: str):
    """设备轮询待处理命令（最多5条）"""
    expire_old_commands(device_id)
    commands = get_pending_commands(device_id)
    return {"code": 0, "data": commands, "server_time": time()}


@app.put("/api/commands/{request_id}")
def update_command(request_id: str, payload: UpdateCommandPayload):
    """设备更新命令状态"""
    valid_statuses = {"received", "completed", "failed", "timeout"}
    if payload.status not in valid_statuses:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid status. Must be one of: {valid_statuses}",
        )
    cmd = update_command_status(
        request_id=request_id,
        status=payload.status,
        result_id=payload.result_id,
    )
    if not cmd:
        raise HTTPException(status_code=404, detail="Command not found")
    return {"code": 0, "message": "status updated", "data": cmd}


@app.get("/api/commands/{request_id}")
def get_command_status(request_id: str):
    """查询命令状态"""
    cmd = get_command(request_id)
    if not cmd:
        raise HTTPException(status_code=404, detail="Command not found")
    return {"code": 0, "data": cmd, "server_time": time()}


# ---------- Web ----------

@app.get("/", response_class=HTMLResponse)
def index():
    """Web 展示页面"""
    index_path = os.path.join(static_dir, "index.html")
    if os.path.isfile(index_path):
        with open(index_path, encoding="utf-8") as f:
            content = f.read()
        return HTMLResponse(
            content=content,
            headers={"Cache-Control": "no-cache, no-store, must-revalidate", "Pragma": "no-cache", "Expires": "0"},
        )
    return HTMLResponse("<h1>KECHENG Sensor Dashboard</h1><p>index.html not found</p>")


@app.get("/dashboard", response_class=HTMLResponse)
def dashboard():
    """实时波形示波器页面"""
    dashboard_path = os.path.join(static_dir, "dashboard.html")
    if os.path.isfile(dashboard_path):
        with open(dashboard_path, encoding="utf-8") as f:
            return f.read()
    return HTMLResponse("<h1>Dashboard</h1><p>dashboard.html not found</p>")