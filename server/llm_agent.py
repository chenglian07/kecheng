"""
Week 4: 自然语言交互 — LLM Agent 模块

功能：
  - 基于 OpenAI SDK（兼容智谱/通义/DeepSeek 等国产大模型）
  - 四个 Function Calling 工具：
    A) get_latest_data   — 查询数据库最新 IMU 记录
    B) request_new_collection — 向板端下发 collect_once 命令
    C) start_reporting   — 向板端下发 set_reporting(on) 恢复持续上报
    D) stop_reporting    — 向板端下发 set_reporting(off) 暂停持续上报
  - chat() 函数封装完整的 Agent 对话循环（含多轮工具调用）

环境变量：
  OPENAI_API_KEY   — 必填，大模型 API Key
  OPENAI_BASE_URL  — 可选，API Base URL（默认 https://api.openai.com/v1）
  LLM_MODEL        — 可选，模型名（默认 gpt-4o-mini）
"""

import json
import os
import sys

from openai import OpenAI

# 确保可以 import database
sys.path.insert(0, os.path.dirname(__file__))

from database import query_recent, create_command

# ---------------------------------------------------------------------------
# 初始化 OpenAI 客户端
# ---------------------------------------------------------------------------

def _get_client() -> OpenAI:
    """构建 OpenAI 客户端（从环境变量读取配置）"""
    api_key = os.environ.get("OPENAI_API_KEY", "")
    base_url = os.environ.get("OPENAI_BASE_URL")  # None 则用 SDK 默认
    if not api_key:
        raise RuntimeError(
            "未设置 OPENAI_API_KEY 环境变量。"
            "请在 PowerShell 中执行: $env:OPENAI_API_KEY='sk-xxx'"
        )
    kwargs = {"api_key": api_key}
    if base_url:
        kwargs["base_url"] = base_url
    return OpenAI(**kwargs)


def _get_model() -> str:
    return os.environ.get("LLM_MODEL", "gpt-4o-mini")


# ---------------------------------------------------------------------------
# Function Calling 工具定义（JSON Schema）
# ---------------------------------------------------------------------------

TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "get_latest_data",
            "description": (
                "查询数据库中最新的一条 IMU 三轴加速度记录。"
                "返回真实的数据值（ax, ay, az，单位 g）和记录的绝对时间（ts_server）。"
                "当用户询问'现在的数据'、'最新的读数'、'上次的测量值'等时调用此工具。"
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": [],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "request_new_collection",
            "description": (
                "向开发板下发一条新的 collect_once 采集命令。"
                "设备会在下次轮询时收到该命令并执行一次采集。"
                "当用户说'重新采集'、'测一下'、'获取新数据'、'帮我采一次'等时调用此工具。"
                "注意：命令刚下发时设备还没采集完，必须告诉用户'已下发指令，请稍后刷新查看'。"
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": [],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "start_reporting",
            "description": (
                "向开发板下发 set_reporting 命令，恢复持续上报数据。"
                "设备收到命令后将开始持续上传 IMU 数据。"
                "当用户说'开始采集'、'开启连续上报'、'看实时数据'、'恢复上报'、'开始上报'等时调用此工具。"
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": [],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "stop_reporting",
            "description": (
                "向开发板下发 set_reporting 命令，暂停持续上报数据。"
                "设备收到命令后将停止持续上传 IMU 数据。"
                "当用户说'停止采集'、'暂停上报'、'别发了'、'停止上报'、'暂停采集'、'关闭上报'等时调用此工具。"
            ),
            "parameters": {
                "type": "object",
                "properties": {},
                "required": [],
            },
        },
    },
]

# ---------------------------------------------------------------------------
# 工具执行函数
# ---------------------------------------------------------------------------

# 默认设备 ID（当数据库无记录时的兜底值）
_DEFAULT_DEVICE = "g02-s20251040108-esp32s3eye"


def _get_active_device() -> str:
    """获取当前活跃设备 ID（从最新记录中取，无则用默认值）"""
    records = query_recent(limit=1)
    return records[0]["device_id"] if records else _DEFAULT_DEVICE


def _tool_get_latest_data() -> dict:
    """工具 A：查询最新 IMU 记录"""
    records = query_recent(limit=1)
    if not records:
        return {
            "success": False,
            "message": "数据库中暂无任何 IMU 数据记录。",
        }
    rec = records[0]
    return {
        "success": True,
        "device_id": rec.get("device_id", ""),
        "ax": rec.get("ax"),
        "ay": rec.get("ay"),
        "az": rec.get("az"),
        "unit": rec.get("unit", "g"),
        "raw_x": rec.get("raw_x"),
        "raw_y": rec.get("raw_y"),
        "raw_z": rec.get("raw_z"),
        "ts_server": rec.get("ts_server", ""),
        "ts_device": rec.get("ts_device"),
        "id": rec.get("id"),
    }


def _tool_request_new_collection() -> dict:
    """工具 B：下发 collect_once 命令"""
    device_id = _get_active_device()

    cmd = create_command(
        device_id=device_id,
        command_type="collect_once",
        timeout_sec=60,
    )
    return {
        "success": True,
        "request_id": cmd.get("request_id", ""),
        "device_id": device_id,
        "status": cmd.get("status", "pending"),
        "created_at": cmd.get("created_at", ""),
        "message": (
            f"已向设备 {device_id} 下发采集命令 "
            f"(request_id={cmd.get('request_id', '')[:8]}…)。"
            "设备将在下次轮询时执行采集，请稍后刷新页面查看新数据。"
        ),
    }


def _tool_start_reporting() -> dict:
    """工具 C：下发 set_reporting(on) 命令，恢复持续上报"""
    device_id = _get_active_device()
    cmd = create_command(
        device_id=device_id,
        command_type="set_reporting",
        params="on",
        timeout_sec=120,
    )
    return {
        "success": True,
        "request_id": cmd.get("request_id", ""),
        "device_id": device_id,
        "action": "start",
        "status": cmd.get("status", "pending"),
        "created_at": cmd.get("created_at", ""),
        "message": (
            f"已向设备 {device_id} 下发「恢复上报」命令 "
            f"(request_id={cmd.get('request_id', '')[:8]}…)。"
            "设备将在下次轮询时恢复持续上传数据，请稍等几秒。"
        ),
    }


def _tool_stop_reporting() -> dict:
    """工具 D：下发 set_reporting(off) 命令，暂停持续上报"""
    device_id = _get_active_device()
    cmd = create_command(
        device_id=device_id,
        command_type="set_reporting",
        params="off",
        timeout_sec=120,
    )
    return {
        "success": True,
        "request_id": cmd.get("request_id", ""),
        "device_id": device_id,
        "action": "stop",
        "status": cmd.get("status", "pending"),
        "created_at": cmd.get("created_at", ""),
        "message": (
            f"已向设备 {device_id} 下发「暂停上报」命令 "
            f"(request_id={cmd.get('request_id', '')[:8]}…)。"
            "设备将在下次轮询时停止持续上传数据，请稍等几秒。"
        ),
    }


# 工具名 → 执行函数映射
_TOOL_DISPATCH = {
    "get_latest_data": _tool_get_latest_data,
    "request_new_collection": _tool_request_new_collection,
    "start_reporting": _tool_start_reporting,
    "stop_reporting": _tool_stop_reporting,
}


# ---------------------------------------------------------------------------
# System Prompt
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = """\
你是 EgoLink 智能传感器助手，负责帮助用户查询和管理 IMU 传感器数据。

## 你的能力
1. **查询最新数据**：当用户询问"现在的数据是多少"、"最新读数"、"上次的测量值"时，
   调用 `get_latest_data` 工具获取数据库中的真实数据，然后把数据以清晰格式呈现给用户。
   注意：你必须使用工具返回的真实数据，绝不能自己编造数据值。
2. **请求新采集**：当用户说"重新采集一下"、"帮我测一次"、"获取新数据"时，
   调用 `request_new_collection` 工具下发采集命令。
   你必须告诉用户"已下发指令，请稍后刷新查看"，因为命令下发后设备需要时间执行。
3. **开始持续上报**：当用户说"开始采集"、"开启连续上报"、"看实时数据"、"恢复上报"、"开始上报"时，
   调用 `start_reporting` 工具下发恢复上报命令。
   你必须告诉用户"已下发开始指令，设备将在几秒内生效"。
4. **停止持续上报**：当用户说"停止采集"、"暂停上报"、"别发了"、"停止上报"、"暂停采集"、"关闭上报"时，
   调用 `stop_reporting` 工具下发暂停上报命令。
   你必须告诉用户"已下发暂停指令，设备将在几秒内生效"。
5. **闲聊与解释**：如果用户的问题与上述四种操作无关（如闲聊、问传感器原理等），
   直接回答，不要调用任何工具。

## 意图判断规则
- "现在的数据"、"当前值" → get_latest_data
- "重新采集"、"测一下" → request_new_collection
- "开始"、"开启"、"恢复"、"启动" + "采集/上报/数据" → start_reporting
- "停止"、"暂停"、"关闭"、"别发" + "采集/上报/数据" → stop_reporting
- 其他 → 直接回复，不调用工具

## 澄清规则（Week 6）
- 当用户的指令意图不明确时（例如只说"查一下"、"弄一下那个"、"帮我看看"、"搞一下"），
  **不要调用任何工具**，而是主动反问用户以澄清意图。
- 澄清问题应引导用户明确是要：
  1. 查看已有的最新数据（对应 get_latest_data）
  2. 让设备重新采集一次新数据（对应 request_new_collection）
  3. 开始/停止持续上报数据（对应 start_reporting / stop_reporting）
- 示例澄清回复："请问您是想查看现有的最新数据，还是想让设备重新采集一次新数据呢？"

## 回答规范
- 使用中文回复。
- 数据值保留 4 位小数。
- 时间以 ts_server 字段为准（ISO 8601 格式），转换为可读格式呈现。
- 不要编造任何传感器数值，所有数据必须来自工具调用结果。
"""


# ---------------------------------------------------------------------------
# 核心对话函数
# ---------------------------------------------------------------------------

def chat(user_message: str, history: list | None = None) -> dict:
    """
    处理用户消息，返回结构化响应。

    参数:
        user_message: 用户输入的自然语言文本
        history: 多轮对话历史（不含 system prompt），格式:
                 [{"role": "user"|"assistant", "content": "..."}, ...]
                 Week 6: 由 main.py 中的 session 管理器传入

    返回:
        {
            "reply": "大模型给用户的最终文本",
            "tool_used": "工具名" | None,
            "data": { ... } | None   # 工具返回的原始数据（如有）
        }
    """
    client = _get_client()
    model = _get_model()

    # 构建消息列表：system + 历史 + 当前用户消息
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
    ]
    if history:
        messages.extend(history)
    messages.append({"role": "user", "content": user_message})

    tool_used = None
    tool_data = None
    max_rounds = 5  # 防止无限循环

    for _ in range(max_rounds):
        response = client.chat.completions.create(
            model=model,
            messages=messages,
            tools=TOOLS,
            tool_choice="auto",
        )

        choice = response.choices[0]
        assistant_msg = choice.message

        # 将助手消息加入历史（保留 tool_calls 信息以便后续轮次引用）
        messages.append(assistant_msg.model_dump())

        # ---------- 没有工具调用 → 最终回复 ----------
        if not assistant_msg.tool_calls:
            return {
                "reply": assistant_msg.content or "",
                "tool_used": tool_used,
                "data": tool_data,
            }

        # ---------- 处理工具调用 ----------
        for tc in assistant_msg.tool_calls:
            fn_name = tc.function.name
            tool_used = fn_name  # 记录最后一次工具名

            # 执行工具
            dispatch_fn = _TOOL_DISPATCH.get(fn_name)
            if dispatch_fn:
                try:
                    result = dispatch_fn()
                except Exception as e:
                    result = {"success": False, "error": str(e)}
            else:
                result = {"success": False, "error": f"未知工具: {fn_name}"}

            tool_data = result

            # 将工具结果作为 tool message 追加
            messages.append({
                "role": "tool",
                "tool_call_id": tc.id,
                "content": json.dumps(result, ensure_ascii=False),
            })

    # 超过 max_rounds 仍未得到最终回复
    return {
        "reply": "抱歉，处理过程中出现了过多轮工具调用，请重试。",
        "tool_used": tool_used,
        "data": tool_data,
    }