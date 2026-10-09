"""
本地代理服务器：
  - GET /           → 返回仪表盘 HTML
  - GET /api/imu    → 代理到远程服务器，避免 CORS
"""
import http.server
import json
import urllib.request
import os

PORT = 8888
REMOTE_BASE = "http://10.1.41.154:8000"
HTML_FILE = os.path.join(os.path.dirname(__file__), "live_dashboard.html")

class ProxyHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/" or self.path == "":
            # 返回本地 HTML
            try:
                with open(HTML_FILE, "r", encoding="utf-8") as f:
                    content = f.read().encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Error: {e}".encode())

        elif self.path.startswith("/api/"):
            # 代理到远程服务器
            remote_url = REMOTE_BASE + self.path
            try:
                req = urllib.request.Request(remote_url)
                with urllib.request.urlopen(req, timeout=10) as resp:
                    data = resp.read()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            except Exception as e:
                err = json.dumps({"error": str(e)}).encode()
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(err)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        print(f"[proxy] {args[0]}")

if __name__ == "__main__":
    print(f"=== 仪表盘代理服务器 ===")
    print(f"打开浏览器访问: http://localhost:{PORT}")
    print(f"远程服务器: {REMOTE_BASE}")
    print(f"HTML 文件: {HTML_FILE}")
    print(f"按 Ctrl+C 停止")
    server = http.server.HTTPServer(("0.0.0.0", PORT), ProxyHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")
        server.server_close()
