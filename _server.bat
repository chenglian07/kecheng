@echo off
cd /d d:\shangke\zhou\server
python -m uvicorn main:app --host 0.0.0.0 --port 8000
pause
