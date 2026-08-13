#!/usr/bin/env python3
"""Apply the libaio recall gate and produce the canonical summary schema."""
import argparse, csv, glob, os

FIELDS='dataset backend destination L beam_width threads cache_nodes actual_cache_hit_ratio mean_io_size mean_batch_size qps mean_latency_us p50_latency_us p95_latency_us p99_latency_us p999_latency_us mean_ios_per_query mean_bytes_per_query read_iops read_bw_MBps cpu_user_pct cpu_sys_pct recall_at_10 run'.split()
KEYS='dataset L beam_width threads cache_nodes run'.split()
def main():
    p=argparse.ArgumentParser(); p.add_argument('inputs',nargs='+'); p.add_argument('-o','--output',default='results/summary.csv'); a=p.parse_args()
    paths=sum((glob.glob(x) for x in a.inputs),[]); rows=[]
    for path in paths:
        with open(path,newline='') as f: rows.extend(csv.DictReader(f))
    baselines={tuple(r[k] for k in KEYS):float(r['recall_at_10']) for r in rows if r['backend']=='libaio'}
    valid=[]
    for r in rows:
        base=baselines.get(tuple(r[k] for k in KEYS))
        if base is None or abs(float(r['recall_at_10'])-base)>1e-6:
            print(f"INVALID recall: {r.get('backend')} {r.get('recall_at_10')} baseline={base}"); continue
        valid.append({k:r.get(k,'') for k in FIELDS})
    os.makedirs(os.path.dirname(a.output) or '.',exist_ok=True)
    with open(a.output,'w',newline='') as f: w=csv.DictWriter(f,FIELDS); w.writeheader(); w.writerows(valid)
    print(f'Recall PASS: {len(valid)} rows; rejected {len(rows)-len(valid)}')
if __name__=='__main__': main()
