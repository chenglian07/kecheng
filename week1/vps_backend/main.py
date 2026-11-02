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
import uuid
import asyncio
from contextlib import asynccontextmanager
from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse

from database import (
    init_db, insert_imu_record, query_latest, query_devices,
    insert_task, get_task_by_request_id, get_pending_task,
    update_task_complete, update_task_failed, mark_timeout_tasks,
)
from models import (
    IMUUpload, UploadResponse, QueryResponse, IMURecord,
    CollectRequest, TaskCompleteReport, TaskFailReport,
    PeriodicControl, TaskStatusResponse, TaskIMUData,
)

# ---- 周期上报状态（内存字典，默认 True = 正常上报） ----
periodic_report_state: dict[str, bool] = {}


@asynccontextmanager
async def lifespan(app: FastAPI):
    """应用生命周期：启动时初始化数据库 + 启动超时检查后台任务"""
    init_db()
    print("[Server] FastAPI 服务已启动")

    async def timeout_checker():
        """每 5 秒扫描一次，将超过 30 秒未响应的任务标记为 timeout"""
        while True:
            try:
                count = mark_timeout_tasks(30)
                if count > 0:
                    print(f"[Timeout] 已将 {count} 个超时任务标记为 timeout")
            except Exception as e:
                print(f"[Timeout] 检查异常: {e}")
            await asyncio.sleep(5)

    task = asyncio.create_task(timeout_checker())
    yield
    task.cancel()


app = FastAPI(
    title="Week2 IMU Data Service",
    description="ESP32-S3-EYE QMA7981 加速度计数据采集与远程指令系统",
    version="2.0.0",
    lifespan=lifespan,
)

# CORS：允许前端跨域（开发阶段）
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


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


# ============ 第二周：远程采集指令接口 ============

@app.post("/api/tasks/collect")
async def create_collect_task(data: CollectRequest):
    """
    Web 前端发起采集指令 → 创建任务记录，返回 request_id
    """
    request_id = str(uuid.uuid4())
    try:
        task_id = insert_task(request_id, data.device_id, "collect_once")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"任务创建失败: {str(e)}")
    print(f"[Task] 创建采集任务: request_id={request_id}, device={data.device_id}")
    return {
        "request_id": request_id,
        "status": "submitted",
        "message": "采集指令已提交，等待设备响应",
    }


@app.get("/api/tasks/pending")
async def poll_pending_task(
    device_id: str = Query(..., description="设备 ID"),
):
    """
    ESP32 轮询拉取待执行指令。
    如果有 submitted 任务，自动标记为 received 并返回。
    同时返回该设备的周期上报状态 periodic_report。
    """
    task = get_pending_task(device_id)
    periodic = periodic_report_state.get(device_id, True)

    if task:
        print(f"[Task] 设备 {device_id} 拉取到任务: {task['request_id']}")
        return {
            "task": {
                "request_id": task["request_id"],
                "command": task["command"],
            },
            "periodic_report": periodic,
        }
    return {
        "task": None,
        "periodic_report": periodic,
    }


@app.post("/api/tasks/{request_id}/complete")
async def report_task_complete(request_id: str, data: TaskCompleteReport):
    """
    ESP32 采集成功，回传 IMU 数据
    """
    ok = update_task_complete(
        request_id=request_id,
        ax_mg=data.ax_mg, ay_mg=data.ay_mg, az_mg=data.az_mg,
        raw_x=data.raw_x, raw_y=data.raw_y, raw_z=data.raw_z,
        chip_id=data.chip_id, timestamp=data.timestamp,
    )
    if not ok:
        raise HTTPException(
            status_code=404,
            detail="任务不存在，或已被完成/超时",
        )
    print(f"[Task] 任务完成: {request_id}")
    return {"status": "ok", "message": "任务结果已保存"}


@app.post("/api/tasks/{request_id}/fail")
async def report_task_fail(request_id: str, data: TaskFailReport):
    """
    ESP32 采集失败，回传错误信息
    """
    ok = update_task_failed(request_id, data.error)
    if not ok:
        raise HTTPException(
            status_code=404,
            detail="任务不存在，或已被完成/超时",
        )
    print(f"[Task] 任务失败: {request_id}, error={data.error}")
    return {"status": "ok", "message": f"失败已记录: {data.error}"}


@app.get("/api/tasks/{request_id}", response_model=TaskStatusResponse)
async def get_task_status(request_id: str):
    """
    Web 前端查询任务状态（轮询此接口直到 status 不再是 submitted/received）
    """
    task = get_task_by_request_id(request_id)
    if not task:
        raise HTTPException(status_code=404, detail="任务不存在")

    imu_data = None
    if task["status"] == "completed" and task["imu_ax_mg"] is not None:
        imu_data = TaskIMUData(
            ax_mg=task["imu_ax_mg"],
            ay_mg=task["imu_ay_mg"],
            az_mg=task["imu_az_mg"],
            raw_x=task["imu_raw_x"],
            raw_y=task["imu_raw_y"],
            raw_z=task["imu_raw_z"],
            chip_id=task["imu_chip_id"],
            timestamp=task["imu_timestamp"],
        )

    return TaskStatusResponse(
        request_id=task["request_id"],
        status=task["status"],
        device_id=task["device_id"],
        command=task["command"],
        created_at=task["created_at"],
        received_at=task["received_at"],
        completed_at=task["completed_at"],
        error_message=task["error_message"],
        imu_data=imu_data,
    )


@app.post("/api/tasks/periodic")
async def control_periodic_report(data: PeriodicControl):
    """
    Web 前端控制设备周期上报：暂停 (pause) 或恢复 (resume)
    暂停后 ESP32 仍保持指令通道（拉取 pending），只是不再主动 POST /api/imu
    """
    if data.action not in ("pause", "resume"):
        raise HTTPException(
            status_code=400,
            detail="action 必须是 'pause' 或 'resume'",
        )
    periodic_report_state[data.device_id] = (data.action == "resume")
    state_str = "恢复" if data.action == "resume" else "暂停"
    print(f"[Periodic] 设备 {data.device_id} 周期上报已{state_str}")
    return {
        "device_id": data.device_id,
        "periodic_report": periodic_report_state[data.device_id],
        "message": f"周期上报已{state_str}",
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