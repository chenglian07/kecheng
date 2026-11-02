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


# ============ 第二周：任务相关模型 ============

class CollectRequest(BaseModel):
    """Web 前端发起的采集请求"""
    device_id: str = Field(..., description="目标设备 ID，如 group_01")


class TaskCompleteReport(BaseModel):
    """ESP32 回传采集成功结果"""
    ax_mg: float
    ay_mg: float
    az_mg: float
    raw_x: int
    raw_y: int
    raw_z: int
    chip_id: int
    timestamp: int


class TaskFailReport(BaseModel):
    """ESP32 回传采集失败"""
    error: str = Field(..., description="失败原因，如 sensor_read_failed")


class PeriodicControl(BaseModel):
    """Web 前端控制周期上报暂停/恢复"""
    device_id: str
    action: str = Field(..., description="pause 或 resume")


class TaskIMUData(BaseModel):
    """任务完成后的 IMU 数据（嵌套在 TaskStatusResponse 中）"""
    ax_mg: float
    ay_mg: float
    az_mg: float
    raw_x: int
    raw_y: int
    raw_z: int
    chip_id: int
    timestamp: int


class TaskStatusResponse(BaseModel):
    """任务状态查询响应"""
    request_id: str
    status: str
    device_id: str
    command: str
    created_at: Optional[str] = None
    received_at: Optional[str] = None
    completed_at: Optional[str] = None
    error_message: Optional[str] = None
    imu_data: Optional[TaskIMUData] = None