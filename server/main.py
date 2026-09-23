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

# 确保可以 import database.py
sys.path.insert(0, os.path.dirname(__file__))

from fastapi import FastAPI, Query, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from database import (
    init_db, insert_sensor_record, query_recent,
    create_command, get_pending_commands, update_command_status, get_command,
    expire_old_commands,
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
    timeout_sec: int = 30


class UpdateCommandPayload(BaseModel):
    status: str  # 'received', 'completed'
    result_id: int | None = None


# ---------- Lifecycle ----------

@app.on_event("startup")
def on_startup():
    init_db()


# ---------- Sensor data API ----------

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


@app.get("/api/commands/{device_id}/pending")
def poll_pending_commands(device_id: str):
    """设备轮询待处理命令（最多5条）"""
    # 先清理过期命令
    expire_old_commands(device_id)
    commands = get_pending_commands(device_id)
    return {"code": 0, "data": commands}


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
    return {"code": 0, "data": cmd}


# ---------- Web ----------

@app.get("/", response_class=HTMLResponse)
def index():
    """Web 展示页面"""
    index_path = os.path.join(static_dir, "index.html")
    if os.path.isfile(index_path):
        with open(index_path, encoding="utf-8") as f:
            return f.read()
    return HTMLResponse("<h1>KECHENG Sensor Dashboard</h1><p>index.html not found</p>")


@app.get("/dashboard", response_class=HTMLResponse)
def dashboard():
    """实时波形示波器页面"""
    dashboard_path = os.path.join(static_dir, "dashboard.html")
    if os.path.isfile(dashboard_path):
        with open(dashboard_path, encoding="utf-8") as f:
            return f.read()
    return HTMLResponse("<h1>Dashboard</h1><p>dashboard.html not found</p>")