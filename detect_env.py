"""检测 ESP-IDF 环境和串口"""
import os, subprocess, glob

print("=" * 60)
print("ESP-IDF 环境检测")
print("=" * 60)

# 检查 IDF_PATH
idf_path = os.environ.get("IDF_PATH", "")
print(f"\nIDF_PATH 环境变量: {idf_path if idf_path else '未设置'}")

# 搜索常见安装路径
search_paths = [
    r"C:\Espressif\frameworks",
    r"D:\Espressif\frameworks",
    r"C:\Users\user\esp",
    r"C:\Users\user\.espressif",
    r"C:\esp",
    r"D:\esp",
]

found_idf = []
for sp in search_paths:
    if os.path.exists(sp):
        print(f"\n发现目录: {sp}")
        for item in os.listdir(sp):
            full = os.path.join(sp, item)
            print(f"  - {item}")
            export = os.path.join(full, "export.bat")
            if os.path.exists(export):
                found_idf.append(export)
                print(f"    → export.bat 存在!")

# 查找 export.bat
if not found_idf:
    print("\n搜索 export.bat ...")
    for drive in ["C:\\", "D:\\"]:
        for pattern in [
            os.path.join(drive, "Espressif", "frameworks", "esp-idf-*", "export.bat"),
            os.path.join(drive, "Users", "user", "esp", "esp-idf", "export.bat"),
            os.path.join(drive, "Users", "user", ".espressif", "*", "export.bat"),
        ]:
            matches = glob.glob(pattern)
            found_idf.extend(matches)

print(f"\n找到的 export.bat:")
for f in found_idf:
    print(f"  {f}")

# 检测串口
print("\n--- 串口检测 ---")
try:
    result = subprocess.run(["mode"], capture_output=True, text=True, timeout=5)
    for line in result.stdout.split("\n"):
        if "COM" in line:
            print(f"  {line.strip()}")
except:
    pass

# 检查项目是否已有 build
fw_dir = r"d:\shangke\zhou\firmware"
if os.path.exists(fw_dir):
    print(f"\n--- 现有固件目录: {fw_dir} ---")
    for item in os.listdir(fw_dir):
        print(f"  {item}")

print("\n" + "=" * 60)
