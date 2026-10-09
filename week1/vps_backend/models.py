"""
models.py - Pydantic 数据模型（请求/响应）
"""

from pydantic import BaseModel, Field
from typing import Optional


class IMUUpload(BaseModel):
    """ESP32 上传的 IMU 数据"""
    device_id: str = Field(..., description="设备 ID，如 group_01")
    timestamp: int = Field(..., description="Unix 时间戳（秒）")
    ax_mg: float = Field(..., description="X 轴加速度（mg）")
    ay_mg: float = Field(..., description="Y 轴加速度（mg）")
    az_mg: float = Field(..., description="Z 轴加速度（mg）")
    raw_x: int = Field(..., description="X 轴原始值")
    raw_y: int = Field(..., description="Y 轴原始值")
    raw_z: int = Field(..., description="Z 轴原始值")
    chip_id: int = Field(..., description="芯片 ID")


class IMURecord(IMUUpload):
    """数据库中的 IMU 记录（含服务端时间）"""
    id: int
    created_at: Optional[str] = None


class UploadResponse(BaseModel):
    """上传响应"""
    status: str = "ok"
    id: int
    message: str = "数据已接收"


class QueryResponse(BaseModel):
    """查询响应"""
    total: int
    device_id: Optional[str] = None
    records: list[IMURecord]