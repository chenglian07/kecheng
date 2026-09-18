"""
传感器数据接收服务 (FastAPI)

端点：
  POST /api/ingest  — 接收传感器 JSON 数据
  GET  /api/data    — 查询最近记录
  GET  /            — Web 展示页面
"""

import os
import sys

# 确保可以 import database.py
sys.path.insert(0, os.path.dirname(__file__))

from fastapi import FastAPI, Query
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from database import init_db, insert_sensor_record, query_recent

app = FastAPI(title="KECHENG Sensor API", version="0.1.0")

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


# ---------- Lifecycle ----------

@app.on_event("startup")
def on_startup():
    init_db()


# ---------- API routes ----------

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
    )
    return {"code": 0, "message": "ok", "id": row_id}


@app.get("/api/data")
def get_data(
    device_id: str | None = Query(None),
    limit: int = Query(default=20, le=100),
):
    """查询最近的传感器数据"""
    records = query_recent(device_id, limit)
    return {"code": 0, "data": records}


@app.get("/", response_class=HTMLResponse)
def index():
    """Web 展示页面"""
    index_path = os.path.join(static_dir, "index.html")
    if os.path.isfile(index_path):
        with open(index_path, encoding="utf-8") as f:
            return f.read()
    return HTMLResponse("<h1>KECHENG Sensor Dashboard</h1><p>index.html not found</p>")