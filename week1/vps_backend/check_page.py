import urllib.request
import time

time.sleep(1)
try:
    r = urllib.request.urlopen('http://127.0.0.1:8000/')
    t = r.read().decode()
    print(f'Page size: {len(t)} bytes')
    checks = ['btnCollect','btnPeriodic','taskPanel','collectOnce',
              'Week 2','my-esp32','newCollectCard','renderNewCollect',
              'togglePeriodic','updatePeriodicUI','pollTaskStatus',
              'TASK_TIMEOUT_SEC','renderNewCollect']
    for k in checks:
        status = 'OK' if k in t else 'MISSING'
        print(f'  {status}: {k}')
    # check API
    r2 = urllib.request.urlopen('http://127.0.0.1:8000/api/health')
    import json
    d = json.loads(r2.read())
    print(f'\nAPI health: {d["status"]}')
    print('\nAll checks passed!' if all(k in t for k in checks) else '\nSOME CHECKS FAILED!')
except Exception as e:
    print(f'Error: {e}')