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

from fastapi import FastAPI, Query, HTTPException, Request, UploadFile, File
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from database import (
    init_db, insert_sensor_record, query_recent,
    create_command, get_pending_commands, update_command_status, get_command,
    expire_old_commands, query_recent_commands,
    # Week 3: 求助事件
    insert_help_event, get_help_event, update_help_event_status,
    query_help_events, expire_help_events,
)

# Week 4: LLM Agent
from llm_agent import chat as llm_chat

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


# Week 3: 求助事件 Pydantic models
class CreateHelpEventPayload(BaseModel):
    event_id: str
    device_id: str
    source: str = "button"  # "button" = 真实按键, "test" = 模拟
    ts_created: float | None = None
    ts_local_ack: float | None = None


class AckHelpEventPayload(BaseModel):
    viewer_name: str | None = None


class CancelHelpEventPayload(BaseModel):
    reason: str | None = None


# Week 4: 自然语言聊天
class ChatPayload(BaseModel):
    message: str
    session_id: str | None = None  # Week 6: 多轮会话支持


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


# ---------- Week 3: 求助事件 API ----------

@app.post("/api/help_events")
def create_help_event(payload: CreateHelpEventPayload):
    """
    板端上报求助事件
    
    服务端成功接收后写入 server_received_at，状态设为 delivered。
    
    curl 测试:
    curl -X POST http://localhost:8000/api/help_events \
      -H "Content-Type: application/json" \
      -d '{"event_id":"SOS-TEST-001","device_id":"g02-s20251040108-esp32s3eye","source":"button"}'
    """
    # 检查 event_id 是否已存在
    existing = get_help_event(payload.event_id)
    if existing:
        return {"code": 0, "message": "event already exists", "data": existing}

    event = insert_help_event(
        event_id=payload.event_id,
        device_id=payload.device_id,
        source=payload.source,
    )
    return {"code": 0, "message": "event created", "data": event, "server_time": time()}


@app.get("/api/help_events")
def list_help_events(
    device_id: str | None = Query(None),
    status: str | None = Query(None),
    limit: int = Query(default=20, le=100),
):
    """
    查询求助事件列表
    
    curl 测试:
    curl http://localhost:8000/api/help_events
    curl "http://localhost:8000/api/help_events?device_id=g02-s20251040108-esp32s3eye"
    curl "http://localhost:8000/api/help_events?status=delivered"
    """
    expire_help_events()
    events = query_help_events(device_id, status, limit)
    return {"code": 0, "data": events, "server_time": time()}


@app.get("/api/help_events/{event_id}")
def get_help_event_status(event_id: str):
    """
    查询单个求助事件状态
    
    curl 测试:
    curl http://localhost:8000/api/help_events/SOS-TEST-001
    """
    event = get_help_event(event_id)
    if not event:
        raise HTTPException(status_code=404, detail="Event not found")
    return {"code": 0, "data": event, "server_time": time()}


@app.put("/api/help_events/{event_id}/ack")
def acknowledge_help_event(event_id: str, payload: AckHelpEventPayload):
    """
    查看者回应求助事件
    
    业务规则：
    - 仅在 status == "delivered" 时允许改为 acknowledged
    - 若已是 cancelled，返回 409 Conflict，不得复活
    
    curl 测试:
    curl -X PUT http://localhost:8000/api/help_events/SOS-TEST-001/ack \
      -H "Content-Type: application/json" \
      -d '{"viewer_name":"teacher"}'
    """
    event = get_help_event(event_id)
    if not event:
        raise HTTPException(status_code=404, detail="Event not found")

    current_status = event["status"]

    # 已取消 → 409
    if current_status == "cancelled":
        raise HTTPException(
            status_code=409,
            detail=f"Event already cancelled, cannot acknowledge. "
                   f"cancelled_at={event.get('cancelled_at')}"
        )

    # 已回应 → 幂等
    if current_status == "acknowledged":
        return {"code": 0, "message": "already acknowledged", "data": event}

    # 仅 delivered 可回应
    if current_status != "delivered":
        raise HTTPException(
            status_code=400,
            detail=f"Cannot acknowledge event in status '{current_status}'. "
                   f"Must be 'delivered'."
        )

    from datetime import datetime, timezone
    now = datetime.now(timezone.utc).isoformat()
    updated = update_help_event_status(event_id, "acknowledged", viewer_ack_at=now)
    return {"code": 0, "message": "acknowledged", "data": updated, "server_time": time()}


@app.put("/api/help_events/{event_id}/cancel")
def cancel_help_event(event_id: str, payload: CancelHelpEventPayload):
    """
    取消求助事件
    
    业务规则：
    - delivered / acknowledged → cancelled
    - 已 cancelled → 幂等（409 不报错，返回当前状态）
    - local_ack / uploading 也可取消
    
    curl 测试:
    curl -X PUT http://localhost:8000/api/help_events/SOS-TEST-001/cancel \
      -H "Content-Type: application/json" \
      -d '{}'
    """
    event = get_help_event(event_id)
    if not event:
        raise HTTPException(status_code=404, detail="Event not found")

    current_status = event["status"]

    # 已取消 → 幂等
    if current_status == "cancelled":
        return {"code": 0, "message": "already cancelled", "data": event}

    # 终态检查（acknowledged 也可取消，failed/timeout 也可取消）
    if current_status in ("delivered", "acknowledged", "local_ack", "uploading", "failed", "timeout"):
        from datetime import datetime, timezone
        now = datetime.now(timezone.utc).isoformat()
        updated = update_help_event_status(event_id, "cancelled", cancelled_at=now)
        return {"code": 0, "message": "cancelled", "data": updated, "server_time": time()}

    raise HTTPException(
        status_code=400,
        detail=f"Cannot cancel event in status '{current_status}'."
    )


@app.get("/api/help_events/{device_id}/pending_response")
def poll_help_event_response(device_id: str):
    """
    板端轮询回应/取消命令
    
    返回该设备所有需要板端处理的求助事件状态变更。
    仅返回 delivered 后已被回应或取消的事件。
    
    curl 测试:
    curl http://localhost:8000/api/help_events/g02-s20251040108-esp32s3eye/pending_response
    """
    expire_help_events()
    # 查询该设备所有已回应或已取消的事件
    conn_events = query_help_events(device_id, None, 50)
    pending = []
    for evt in conn_events:
        if evt["status"] in ("acknowledged", "cancelled"):
            pending.append({
                "event_id": evt["event_id"],
                "command_type": "sos_response" if evt["status"] == "acknowledged" else "sos_cancel",
                "status": evt["status"],
                "viewer_ack_at": evt.get("viewer_ack_at"),
                "cancelled_at": evt.get("cancelled_at"),
            })
    return {"code": 0, "data": pending, "server_time": time()}


# ---------- Week 4/6: 自然语言聊天 API ----------

# Week 6: 内存会话管理（每个 session_id 保留最近 5 轮对话）
import uuid

_CHAT_SESSIONS: dict[str, list] = {}   # session_id → [{"role", "content"}, ...]
_MAX_HISTORY_ROUNDS = 5                 # 保留最近 5 轮（10 条消息：5 user + 5 assistant）


@app.post("/api/chat")
def api_chat(payload: ChatPayload):
    """
    自然语言交互接口（Week 4 基础 + Week 6 多轮会话）

    接收用户消息，调用大模型 + Function Calling 处理，返回结构化回复。
    支持 session_id 多轮对话上下文，服务端保留最近 5 轮历史。

    请求体: {"message": "...", "session_id": "可选，不传则自动生成"}

    返回: {"code":0, "reply":"...", "tool_used":"...", "data":{...},
           "request_id":"uuid", "session_id":"..."}

    curl 测试:
    curl -X POST http://localhost:8000/api/chat \
      -H "Content-Type: application/json" \
      -d '{"message": "现在的数据是多少？"}'
    """
    # Week 6: session 管理
    session_id = payload.session_id or str(uuid.uuid4())
    history = _CHAT_SESSIONS.get(session_id, [])

    # Week 6: 每次请求生成唯一 request_id
    request_id = str(uuid.uuid4())

    try:
        result = llm_chat(payload.message, history=history)

        # Week 6: 将本轮对话存入历史（只保留 user/assistant 的文本内容）
        user_turn = {"role": "user", "content": payload.message}
        assistant_turn = {"role": "assistant", "content": result["reply"] or "(工具调用，无文字回复)"}
        history.append(user_turn)
        history.append(assistant_turn)
        # 保留最近 _MAX_HISTORY_ROUNDS 轮（每轮 2 条消息）
        max_msgs = _MAX_HISTORY_ROUNDS * 2
        if len(history) > max_msgs:
            history = history[-max_msgs:]
        _CHAT_SESSIONS[session_id] = history

        return {
            "code": 0,
            "reply": result["reply"],
            "tool_used": result["tool_used"],
            "data": result["data"],
            "request_id": request_id,
            "session_id": session_id,
            "server_time": time(),
        }
    except RuntimeError as e:
        # 环境变量未配置等运行时错误
        raise HTTPException(status_code=500, detail=str(e))
    except Exception as e:
        # 大模型 API 调用失败等
        raise HTTPException(status_code=502, detail=f"LLM 调用失败: {e}")


# ---------- Week 5: 语音识别 API ----------

@app.post("/api/speech-to-text")
async def api_speech_to_text(audio: UploadFile = File(...)):
    """
    语音识别接口（Week 5）

    接收前端上传的音频文件，调用 Whisper API 进行语音转文字。

    环境变量:
      OPENAI_API_KEY       — 必填
      OPENAI_BASE_URL      — 可选（用于 Whisper，默认 OpenAI 官方）
      WHISPER_BASE_URL     — 可选（单独指定 Whisper 的 API 地址）
      WHISPER_API_KEY      — 可选（单独指定 Whisper 的 API Key）
      WHISPER_MODEL        — 可选，模型名（默认 whisper-1）

    curl 测试:
    curl -X POST http://localhost:8000/api/speech-to-text \
      -F "audio=@test.webm;type=audio/webm"
    """
    from openai import OpenAI

    # Whisper 专用配置，回退到通用配置
    whisper_key = os.environ.get("WHISPER_API_KEY") or os.environ.get("OPENAI_API_KEY", "")
    whisper_base = os.environ.get("WHISPER_BASE_URL") or os.environ.get("OPENAI_BASE_URL")
    whisper_model = os.environ.get("WHISPER_MODEL", "whisper-1")

    if not whisper_key:
        raise HTTPException(status_code=500, detail="未配置 OPENAI_API_KEY 或 WHISPER_API_KEY")

    kwargs = {"api_key": whisper_key}
    if whisper_base:
        kwargs["base_url"] = whisper_base

    client = OpenAI(**kwargs)

    # 读取上传的音频数据
    audio_bytes = await audio.read()
    if not audio_bytes:
        raise HTTPException(status_code=400, detail="音频文件为空")

    # 确定文件名后缀（从 content_type 推断）
    suffix = ".webm"
    ct = audio.content_type or ""
    if "wav" in ct:
        suffix = ".wav"
    elif "mp3" in ct or "mpeg" in ct:
        suffix = ".mp3"
    elif "ogg" in ct:
        suffix = ".ogg"
    elif "m4a" in ct or "mp4" in ct:
        suffix = ".m4a"

    import io
    audio_file = io.BytesIO(audio_bytes)
    audio_file.name = f"audio{suffix}"  # OpenAI SDK 需要 name 属性

    try:
        transcript = client.audio.transcriptions.create(
            model=whisper_model,
            file=audio_file,
            language="zh",
        )
        text = transcript.text.strip() if hasattr(transcript, "text") else str(transcript)
        return {"code": 0, "text": text}
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"语音识别失败: {e}")


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