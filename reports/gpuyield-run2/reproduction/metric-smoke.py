import importlib.util
from pathlib import Path
import tempfile
spec = importlib.util.spec_from_file_location('yield_metric', 'tools/summarize-gpu-yield.py')
metric = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metric)
with tempfile.TemporaryDirectory(dir='.tmp/gpuyield-run2') as temp:
    root = Path(temp)
    log = root / 'producer.log'
    def producer(frames, duration, valid=True):
        rows = []
        for frame in range(frames):
            rows += [f'[GPUYIELD_CHUNK] start_ticks={1 if valid else 0} end_ticks=3 dispatch_start_ticks=1 dispatch_end_ticks=2 dispatch_gpu_ms={duration} elapsed_gpu_ms={duration}',
                     f'[GPUYIELD] submissions=1 max_submission_ms={duration} budget_ms=16.666666']
        log.write_text('\n'.join(rows))
        return metric.summarize(log, None, 500)['budget_pass']
    assert producer(500, 16.666666)
    assert not producer(499, 1)
    assert not producer(500, 16.666667)
    assert not producer(500, 1, False)
    csv = root / 'peer.csv'
    header = 'gpu_elapsed_ms,submit_call_ms,fence_wait_ms,service_ms,cadence_ms\n'
    csv.write_text(header)
    assert not metric.summarize(None, csv, 500)['peer_valid']
    csv.write_text(header + '.001,.1,.1,.2,16.7\n' * 2)
    assert metric.summarize(None, csv, 500)['peer_valid']
    csv.write_text(header + '.001,.1,.1,.2,16.7\n' + 'nan,.1,.1,.2,16.7\n')
    assert not metric.summarize(None, csv, 500)['peer_valid']
    csv.write_text(header + '.001,.1,.1,.2,16.7\n' + '0.001,.1\n')
    assert metric.summarize(None, csv, 500)['peer_invalid_rows'] == 1
print('Timing smoke checks: completed frames, budget boundary, query availability, empty/invalid peer rows PASS')
