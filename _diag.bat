@echo off
chcp 65001 >nul
set OUT=d:\shangke\zhou\_result.txt
echo === 网络诊断 > %OUT%
echo. >> %OUT%

echo [1] 检查8000端口... >> %OUT%
netstat -ano | findstr ":8000" >> %OUT% 2>&1
echo. >> %OUT%

echo [2] 检查本机IP... >> %OUT%
ipconfig | findstr /C:"IPv4" >> %OUT% 2>&1
echo. >> %OUT%

echo [3] 启动服务器... >> %OUT%
start "" /B python -m uvicorn main:app --host 0.0.0.0 --port 8000
echo 等待5秒... >> %OUT%
timeout /t 5 /nobreak >nul
echo. >> %OUT%

echo [4] 再次检查8000端口... >> %OUT%
netstat -ano | findstr ":8000" >> %OUT% 2>&1
echo. >> %OUT%

echo [5] 检查防火墙规则... >> %OUT%
netsh advfirewall firewall show rule name="EgoLink-8000" >> %OUT% 2>&1
echo. >> %OUT%

echo [6] localhost测试... >> %OUT%
python -c "import urllib.request,json; r=urllib.request.urlopen('http://127.0.0.1:8000/api/data?limit=1',timeout=3); d=json.loads(r.read()); print('OK, records:', len(d.get('data',[])))" >> %OUT% 2>&1
echo. >> %OUT%

echo [7] POST测试... >> %OUT%
python -c "import urllib.request,json,time; p=json.dumps({'device_id':'diag','sensor':'qma7981','ts_device':time.time(),'ax':0.01,'ay':-0.02,'az':0.98,'unit':'g','raw':[100,-200,8000],'status':'ok'}).encode(); req=urllib.request.Request('http://127.0.0.1:8000/api/ingest',data=p,headers={'Content-Type':'application/json'}); r=urllib.request.urlopen(req,timeout=3); d=json.loads(r.read()); print('OK, id:',d.get('id'))" >> %OUT% 2>&1
echo. >> %OUT%

echo [8] LAN IP测试... >> %OUT%
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /C:"IPv4"') do (
    for /f "tokens=1" %%b in ("%%a") do (
        python -c "import urllib.request; r=urllib.request.urlopen('http://%%b:8000/',timeout=3); print('LAN %%b: OK',len(r.read()),'B')" >> %OUT% 2>&1
    )
)
echo. >> %OUT%

echo === 诊断完成 >> %OUT%
echo DONE
