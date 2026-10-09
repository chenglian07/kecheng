"""
main.py - FastAPI 后端入口

功能：
  POST /api/imu        - 接收 ESP32 上传的 IMU 数据，存入 SQLite
  GET  /api/imu        - 查询 IMU 数据（支持 ?device_id=xxx&limit=50）
  GET  /api/devices    - 查询所有设备 ID
  GET  /api/health     - 健康检查
  GET  /               - 前端页面（静态文件）

启动：
  uvicorn main:app --host 0.0.0.0 --port 8000 --reload
"""

import os
import time
from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse

from database import init_db, insert_imu_record, query_latest, query_devices
from models import IMUUpload, UploadResponse, QueryResponse, IMURecord

app = FastAPI(
    title="Week1 IMU Data Service",
    description="ESP32-S3-EYE QMA7981 加速度计数据采集与展示系统",
    version="1.0.0",
)

# CORS：允许前端跨域（开发阶段）
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
async def startup_event():
    """服务启动时初始化数据库"""
    init_db()
    print("[Server] FastAPI 服务已启动")


# ============ API 路由 ============

@app.post("/api/imu", response_model=UploadResponse)
async def upload_imu(data: IMUUpload):
    """
    接收 ESP32 上传的 IMU 数据
    
    - 验证字段完整性
    - 存入 SQLite
    - 返回记录 ID
    """
    try:
        record_id = insert_imu_record(
            device_id=data.device_id,
            timestamp=data.timestamp,
            ax_mg=data.ax_mg,
            ay_mg=data.ay_mg,
            az_mg=data.az_mg,
            raw_x=data.raw_x,
            raw_y=data.raw_y,
            raw_z=data.raw_z,
            chip_id=data.chip_id,
        )
        return UploadResponse(
            status="ok",
            id=record_id,
            message=f"数据已接收 (ID={record_id}, 设备={data.device_id})"
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"数据库写入失败: {str(e)}")


@app.get("/api/imu", response_model=QueryResponse)
async def get_imu_data(
    device_id: str = Query(None, description="按设备 ID 过滤"),
    limit: int = Query(50, ge=1, le=500, description="返回记录数上限"),
):
    """
    查询 IMU 数据
    
    - 默认返回所有设备最新 50 条
    - 可用 device_id 参数过滤
    """
    records = query_latest(device_id=device_id, limit=limit)
    return QueryResponse(
        total=len(records),
        device_id=device_id,
        records=[IMURecord(**r) for r in records],
    )


@app.get("/api/devices")
async def get_devices():
    """查询所有已注册的设备 ID 列表"""
    devices = query_devices()
    return {"devices": devices, "count": len(devices)}


@app.get("/api/health")
async def health_check():
    """健康检查"""
    return {
        "status": "ok",
        "server_time": int(time.time()),
        "message": "IMU 数据服务运行中",
    }


# ============ 静态文件：前端页面 ============

# 前端文件目录（相对于后端运行位置）
FRONTEND_DIR = os.path.join(os.path.dirname(__file__), "..", "web_frontend")

@app.get("/")
async def serve_frontend():
    """提供前端页面"""
    index_path = os.path.join(FRONTEND_DIR, "index.html")
    if os.path.exists(index_path):
        return FileResponse(index_path, media_type="text/html")
    return {"message": "前端页面未找到，请将 index.html 放入 web_frontend/ 目录"}