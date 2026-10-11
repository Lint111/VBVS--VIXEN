import csv, importlib.util, json, re, shutil, statistics
from pathlib import Path
root=Path.cwd()
out=root/'reports/gpuyield-run4'
native=Path('/mnt/c/vixen-native/gpuyield/reports/gpuyield-run4')
for label in ('native-unbounded','native-final','native-unbounded-1080','native-final-1080','native-pacing-500','native-pacing-1080','native-final-no-trace','native-final-1080-no-trace'):
    if (native/label).exists(): shutil.copytree(native/label,out/label,dirs_exist_ok=True)
if (native/'native-hardware-ray-tests.log').exists(): shutil.copy2(native/'native-hardware-ray-tests.log',out)
spec=importlib.util.spec_from_file_location('gpu_yield',root/'tools/summarize-gpu-yield.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
result={'warmup_frames_excluded':10,'steady_frames':490,'scale':1.,'host_isolation':'GPU device serialized; shared CPU cohort', 'runs':{}}
for label in ('native-unbounded','native-final','native-unbounded-1080','native-final-1080','native-pacing-500','native-pacing-1080','native-final-no-trace','native-final-1080-no-trace','dozen-final'):
    folder=out/label
    if not (folder/'status.json').exists() or not (folder/'vixen-perf.csv').exists(): continue
    run={'status':json.loads((folder/'status.json').read_text(encoding='utf-8-sig'))}
    with (folder/'vixen-perf.csv').open(encoding='utf-8-sig') as f: rows=list(csv.DictReader(f))
    run['csv_frames']=len(rows)
    steady=rows[10:]
    run['per_pass_ms']={key:module.distribution([float(row[key]) for row in steady]) for key in rows[0] if key.endswith('_ms')}
    text='\n'.join((folder/file).read_text(errors='replace') for file in ('vixen.log','host.log'))
    combined=folder/'combined.log';combined.write_text(text)
    nodes=sorted(set(re.findall(r'\[GPUYIELD\] node=([^ ]+)',text)))
    run['nodes']={node:module.summarize(combined,None,500,node,6. if node in ('test_dispatch','esvo_traverse_shade') and label.startswith('native-') else None) for node in nodes}
    run['host']=module.summarize(combined,None,500)['host_calls']
    run['host_totals_ms_per_frame']={key:value['mean_ms']*value['samples']/500 for key,value in run['host'].items()}
    result['runs'][label]=run
(out/'driver-matrix.json').write_text(json.dumps(result,indent=2)+'\n')
lines=['Scale 1.0, 500 frames; per-pass GPU-clock spans after excluding 10 warmup frames.','', '| Resolution / pass | Unbounded ms | Tuned bounded ms | Change |','|---|---:|---:|---:|']
for resolution,base,final in [('500×500','native-unbounded','native-final'),('1920×1080','native-unbounded-1080','native-final-1080')]:
    if base not in result['runs'] or final not in result['runs']:continue
    a=result['runs'][base]['per_pass_ms'];b=result['runs'][final]['per_pass_ms']
    for key in a:
        if key not in b:continue
        x=a[key]['mean_ms'];y=b[key]['mean_ms'];pct=(y/x-1)*100 if x else 0
        lines.append(f'| {resolution} / {key} | {x:.6f} | {y:.6f} | {pct:+.1f}% |')
(out/'overhead-table.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
for label,run in result['runs'].items():
    print(label,'exit',run['status']['exit'],'CSV frames',run['csv_frames'])
    for node,data in run['nodes'].items():
        print(' ',node,'gate',data['budget_pass'],'aggregate',data.get('aggregate_budget_pass'),'chunks',data['chunks'],'submissions/frame',data['submissions_per_frame'])
