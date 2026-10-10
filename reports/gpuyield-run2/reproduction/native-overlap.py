import json
import os
from pathlib import Path
import subprocess
import time
root = Path.cwd()
out = root / 'reports/gpuyield-run2/native-under-editor'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
peer = None
heavy = None
success = False
state = {'measurement': 'native AMD Vulkan completion peer during Dozen VIXEN', 'native_producer': False}
try:
    with (out / 'vixen.log').open('w') as app_log, (out / 'probe.log').open('w') as peer_log:
        environment = dict(os.environ, VIXEN_GPUYIELD_TRACE='1', VIXEN_EDITOR_TEST_CAMERA='top-down', VIXEN_EXIT_AFTER_FRAMES='500', VIXEN_PERF_CSV=str(out / 'vixen-perf.csv'))
        heavy = subprocess.Popen(['./vixen_editor'], cwd=root / 'build/gpuyield/binaries', env=environment, stdout=app_log, stderr=subprocess.STDOUT)
        while '[Run] Entering render loop' not in (out / 'vixen.log').read_text(errors='replace'):
            if heavy.poll() is not None: raise RuntimeError('Producer ended before the render marker')
            if time.monotonic() - started > 240: raise TimeoutError('Producer startup timed out')
            time.sleep(.05)
        state['peer_launch_after_render_marker_seconds'] = time.monotonic() - started
        peer = subprocess.Popen(['bash', '.tmp/gpuyield-run2/probe-native-under-editor.sh'], stdout=peer_log, stderr=subprocess.STDOUT)
        while peer.poll() is None:
            if heavy.poll() is not None: raise RuntimeError('Native peer did not fit entirely inside producer lifetime')
            if time.monotonic() - started > 240: raise TimeoutError('Native overlap timed out')
            time.sleep(.05)
        state['peer_exit'] = peer.returncode
        state['peer_finished_while_producer_running'] = heavy.poll() is None
        if peer.returncode != 0 or not state['peer_finished_while_producer_running']: raise RuntimeError('Invalid native overlap')
        state['peer_finished_seconds'] = time.monotonic() - started
        heavy.wait(timeout=max(1, 240 - (time.monotonic() - started)))
        state['producer_exit'] = heavy.returncode
        if heavy.returncode != 0: raise RuntimeError('Producer failed')
        state['wall_seconds'] = time.monotonic() - started
        success = True
finally:
    state['complete_overlap'] = success
    (out / 'overlap.json').write_text(json.dumps(state, indent=2) + '\n')
    if peer is not None and peer.poll() is None:
        cleanup = subprocess.check_output(['wslpath', '-w', str(root / '.tmp/gpuyield-run2/stop-native-peer.ps1')], text=True).strip()
        subprocess.run(['/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', cleanup], check=True)
        peer.terminate()
    if heavy is not None and heavy.poll() is None: heavy.terminate()
    for child in (peer, heavy):
        if child is not None:
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill(); child.wait()
subprocess.run(['python3', 'tools/summarize-gpu-yield.py', '--log', str(out / 'vixen.log'), '--probe', str(out / 'probe.csv'), '--output', str(out / 'timing.json')], check=True)
