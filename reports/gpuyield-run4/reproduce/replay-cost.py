import collections,json,math,re,sys
from pathlib import Path
log=Path(sys.argv[1]);out=Path(sys.argv[2]);width=int(sys.argv[3]);height=int(sys.argv[4])
x,y=(width+7)//8,(height+7)//8
unit=fixed=anchor=0
edges={i:7 for i in range(4)}
chunks=[];batches=[];frames=[];mismatch=[]
for line in log.read_text(errors='replace').splitlines():
 if '[GPUYIELD_CHUNK]' in line:
  f=dict(re.findall(r'(\w+)=([^ ]+)',line));chunks.append(f)
 elif '[GPUYIELD_SUBMISSION]' in line:
  batches.append(dict(re.findall(r'(\w+)=([^ ]+)',line)))
 elif '[GPUYIELD]' in line:
  f=dict(re.findall(r'(\w+)=([^ ]+)',line));flight=int(chunks[0]['flight']);edge=edges[flight]
  areas=[min(edge,x-bx)*min(edge,y-by) for by in range(0,y,edge) for bx in range(0,x,edge)]
  if len(areas)!=len(chunks):
   mismatch.append({'frame':len(frames),'flight':flight,'predicted_edge':edge,'predicted_count':len(areas),'actual_count':len(chunks)})
   # Continue only when the square-grid observation identifies its launch size.
   candidates=[e for e in range(1,max(x,y)+1) if math.ceil(x/e)*math.ceil(y/e)==len(chunks)]
   if len(candidates)!=1:break
   edge=candidates[0];areas=[min(edge,x-bx)*min(edge,y-by) for by in range(0,y,edge) for bx in range(0,x,edge)]
  nominal=min(edge,x)*min(edge,y);full_smaller_over2=0
  for c,area in zip(chunks,areas):
   upper=float(c['elapsed_gpu_ms'])*1e6*1.1
   if area==nominal and area>anchor:
    nxt=upper/area;fixed+=max(0,unit-nxt)*anchor;unit=nxt;anchor=area
   elif area==anchor:unit=max(unit,upper/area)
   else:
    full_smaller_over2+=area==nominal and upper>2e6
    fixed=max(fixed,upper-unit*area)
  for b in batches:
   first,count=int(b['first_chunk']),int(b['chunks']);groups=sum(areas[first:first+count]);elapsed=float(b['elapsed_gpu_ms'])*1e6
   fixed=max(fixed,(elapsed*1.1-unit*groups)/count)
  throughput=math.sqrt(1.9e6/unit)
  nextedge=max(1,int(min(max(x,y),throughput if fixed<1.9e6 else min(throughput,math.sqrt(anchor)))))
  frames.append({'index':len(frames),'flight':flight,'measured_edge':edge,'next_edge':nextedge,'unit_ns':unit,'fixed_ns':fixed,'anchor_units':anchor,'full_smaller_over2_ignored_as_throughput':full_smaller_over2,'max_submission_ms':float(f['max_submission_ms'])})
  edges[flight]=nextedge;chunks=[];batches=[]
result={'assumed_actual_extent':[width,height],'frames_replayed':len(frames),'launch_prediction_mismatches':mismatch,'ignored_hot_full_tiles':sum(f['full_smaller_over2_ignored_as_throughput'] for f in frames),'frames':frames}
out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='frames'},indent=2));print('First:',frames[:5]);print('Last:',frames[-5:])
