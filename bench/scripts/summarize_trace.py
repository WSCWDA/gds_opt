#!/usr/bin/env python3
"""Characterize unmodified AlignedRead records emitted by DiskANN."""
import argparse, csv, json, math
from collections import Counter, defaultdict

def percentile(values, p):
    if not values: return 0.0
    values = sorted(values); x=(len(values)-1)*p; lo=math.floor(x); hi=math.ceil(x)
    return values[lo] if lo == hi else values[lo]*(hi-x)+values[hi]*(x-lo)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('trace'); ap.add_argument('--output'); ap.add_argument('--io-size-csv')
    a=ap.parse_args()
    with open(a.trace, newline='') as f: rows=list(csv.DictReader(f))
    required={'query_id','batch_id','request_in_batch','batch_size','offset','len'}
    if not rows or not required.issubset(rows[0]): raise SystemExit('trace is empty or missing required real-I/O fields')
    requests=[{k:int(r[k]) for k in required} for r in rows]
    by_query=defaultdict(list); by_batch=defaultdict(list)
    for r in requests:
        by_query[r['query_id']].append(r); by_batch[(r['query_id'],r['batch_id'])].append(r)
    offsets=[r['offset'] for r in requests]; sizes=Counter(r['len'] for r in requests)
    batches=[len(v) for v in by_batch.values()]; ios_q=[len(v) for v in by_query.values()]
    bytes_q=[sum(r['len'] for r in v) for v in by_query.values()]
    intra=sum(len(v)-len({r['offset'] for r in v}) for v in by_query.values())
    owners=defaultdict(set)
    for q, rs in by_query.items():
        for r in rs: owners[r['offset']].add(q)
    inter=sum(1 for qs in owners.values() if len(qs)>1)
    adjacent=sum(1 for a,b in zip(requests,requests[1:]) if b['offset']==a['offset']+a['len'])
    jumps=[abs(b['offset']-a['offset']) for a,b in zip(requests,requests[1:])]
    out={'total_queries':len(by_query),'total_io':len(requests),'mean_io_per_query':sum(ios_q)/len(ios_q),
      'mean_bytes_per_query':sum(bytes_q)/len(bytes_q),'mean_batch_size':sum(batches)/len(batches),
      'batch_p50':percentile(batches,.5),'batch_p95':percentile(batches,.95),'batch_p99':percentile(batches,.99),
      'batch_max':max(batches),'unique_offsets':len(set(offsets)),
      'offset_reuse_ratio':1-len(set(offsets))/len(offsets),'intra_query_repeated_requests':intra,
      'inter_query_reused_offsets':inter,'sequential_adjacency_ratio':adjacent/max(1,len(requests)-1),
      'mean_random_jump_bytes':sum(jumps)/max(1,len(jumps)),'batches_per_query_mean':len(batches)/len(by_query),
      'io_size_distribution':{str(k):v for k,v in sorted(sizes.items())}}
    text=json.dumps(out,indent=2); print(text)
    if a.output: open(a.output,'w').write(text+'\n')
    if a.io_size_csv:
        with open(a.io_size_csv,'w',newline='') as f:
            w=csv.writer(f); w.writerow(['size_bytes','count','ratio'])
            for size,count in sorted(sizes.items()): w.writerow([size,count,count/len(requests)])
if __name__=='__main__': main()
