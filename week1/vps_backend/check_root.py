"""检查根路由是否能正常返回前端页面"""
import sys
sys.path.insert(0, r"d:\shangke\zhou\week1\vps_backend")

from main import app, FRONTEND_DIR
import os

print("=" * 60)
print("根路由检查")
print("=" * 60)

# 1. 检查路径解析
print(f"\n[1] FRONTEND_DIR 解析结果:")
print(f"    __file__ = {__file__}")
print(f"    os.path.dirname(__file__) = {os.path.dirname(__file__)}")
print(f"    FRONTEND_DIR = {FRONTEND_DIR}")
print(f"    绝对路径 = {os.path.abspath(FRONTEND_DIR)}")

# 2. 检查 index.html 是否存在
index_path = os.path.join(FRONTEND_DIR, "index.html")
print(f"\n[2] index.html 路径:")
print(f"    index_path = {index_path}")
print(f"    绝对路径 = {os.path.abspath(index_path)}")
print(f"    文件存在 = {os.path.exists(index_path)}")

if os.path.exists(index_path):
    print(f"    文件大小 = {os.path.getsize(index_path)} bytes")
else:
    print(f"    ❌ 文件不存在！这就是 404 的原因！")

# 3. 检查路由注册
print(f"\n[3] 已注册的路由:")
for route in app.routes:
    if hasattr(route, 'path'):
        methods = getattr(route, 'methods', set())
        print(f"    {list(methods)[0] if methods else 'ANY':6} {route.path}")

# 4. 检查 / 路由是否存在
root_routes = [r for r in app.routes if hasattr(r, 'path') and r.path == '/']
print(f"\n[4] 根路由 / :")
if root_routes:
    print(f"    ✅ 已注册")
else:
    print(f"    ❌ 未注册！这就是 404 的原因！")

print("\n" + "=" * 60)
