#!/usr/bin/env python3
import argparse
import json
import re
import tarfile
from pathlib import Path
import shutil
import numpy as np
import hashlib
from tqdm import tqdm
from concurrent.futures import ProcessPoolExecutor, as_completed
import os

# --- CONFIGURACIÓN DE RENDIMIENTO ---
# Cambia este número para limitar manualmente (ej. MAX_PROCESSES = 4)
# O usa os.cpu_count() // 2 para usar la mitad de tu CPU
MAX_PROCESSES = max(1, os.cpu_count() // 2) 

NOSIMRESULTS = "0,0,0,-1,-1,-1,-1,-1,-1,-1,-1"
NOEXISTINGFLOW_TRAFFIC = "-1,0,-1,0,0"

TIME_DIST_CODE = {
    "EXPONENTIAL_T": 0,
    "DETERMINISTIC_T": 1,
    "UNIFORM_T": 2,
    "NORMAL_T": 3,
    "ONOFF_T": 4,
    "PPBP_T": 5,
}

# =========================
# UTILS TAR
# =========================
def read_file_from_tar(tar, member_name):
    try:
        f = tar.extractfile(member_name)
        if f is None:
            return None
        return f.read().decode("utf-8")
    except KeyError:
        return None

# =========================
# PARSERS Y MÉTRICAS (Workers)
# =========================
def parse_tx_rx_from_tar(tar, base_name, flow_ids):
    flows = {}
    for fid in flow_ids:
        tx, rx = {}, {}
        tx_content = read_file_from_tar(tar, f"{base_name}/txfileflow{fid}.txt")
        rx_content = read_file_from_tar(tar, f"{base_name}/rxfileflow{fid}.txt")
        
        if tx_content:
            for line in tx_content.splitlines():
                p = line.strip().split()
                if len(p) >= 3:
                    tx[int(p[0])] = (float(p[1]), int(p[2]))
        if rx_content:
            for line in rx_content.splitlines():
                p = line.strip().split()
                if len(p) >= 3:
                    rx[int(p[0])] = (float(p[1]), int(p[2]))
        flows[fid] = {"tx": tx, "rx": rx}
    return flows

def flow_metrics(flow_data, sim_time):
    tx, rx = flow_data["tx"], flow_data["rx"]
    delays, sizes = [], []
    for pkt, (rt_fs, rsz) in rx.items():
        if pkt in tx:
            d = (rt_fs - tx[pkt][0]) / 1e9
            if d > 0:
                delays.append(d)
                sizes.append(rsz)
    
    pkts_gen_total = len(tx)
    pkts_rx_total = len(rx)
    pkts_drop_total = max(pkts_gen_total - pkts_rx_total, 0)

    if delays:
        arr = np.array(delays)
        avg_delay = float(np.mean(arr))
        avg_ln_delay = float(np.mean(np.log(arr)))
        p10, p20, p50, p80, p90 = [float(np.percentile(arr, q)) for q in (10,20,50,80,90)]
        jitter = float(np.var(arr)) if len(arr) > 1 else 0.0
    else:
        avg_delay = avg_ln_delay = p10 = p20 = p50 = p80 = p90 = -1.0
        jitter = 0.0

    avg_bw_kbps = (sum(sizes) * 8.0) / (sim_time * 1000.0) if sim_time > 0 else 0.0
    pkts_gen = pkts_gen_total / sim_time if sim_time > 0 else 0.0
    pkts_drop = pkts_drop_total / sim_time if sim_time > 0 else 0.0

    return {
        "avg_bw_kbps": avg_bw_kbps, "pkts_gen": pkts_gen, "pkts_drop": pkts_drop,
        "avg_delay": avg_delay, "avg_ln_delay": avg_ln_delay,
        "p10": p10, "p20": p20, "p50": p50, "p80": p80, "p90": p90,
        "jitter": jitter, "delays": delays
    }

def encode_traffic_flow(flow):
    tdist, sdist, tos = flow["TimeDist"], flow["SizeDist"], flow["DSCP"]
    ttype = tdist["Type"]
    tcode = TIME_DIST_CODE[ttype]
    
    if ttype == "ONOFF_T":
        time_part = f"{tcode},{tdist['EqLambda']},{tdist['pktsLambdaOn']},{tdist['avgTOff']},{tdist['avgTOn']},{tdist.get('ExpMaxFactor',1.0)}"
    elif ttype == "EXPONENTIAL_T":
        time_part = f"{tcode},{tdist['EqLambda']},{tdist['AvgPktsLambda']},{tdist.get('ExpMaxFactor',1.0)}"
    elif ttype == "DETERMINISTIC_T":
        time_part = f"{tcode},{tdist['EqLambda']},{tdist['AvgPktsLambda']}"
    else: raise ValueError(f"TimeDist no soportada: {ttype}")
    
    return f"{time_part},0,{sdist['AvgPktSize']},{tos}"

def encode_result_metric(m):
    return ",".join(map(str, [m['avg_bw_kbps'], m['pkts_gen'], m['pkts_drop'], m['avg_delay'], m['avg_ln_delay'], m['p10'], m['p20'], m['p50'], m['p80'], m['p90'], m['jitter']]))

def build_lines(traffic_json, per_flow_metrics, sim_time):
    n = int(traffic_json["NoN"])
    flows = traffic_json["FeaturesperFlow"]
    by_pair = {}
    for f in flows: by_pair.setdefault((int(f["SourceNode"]), int(f["DestinationNode"])), []).append(f)

    cells_traffic, cells_results, cells_flow_results = [], [], []
    g_packets = g_losses = g_delay_weighted = g_delay_count = 0.0

    for src in range(n):
        for dst in range(n):
            flist = by_pair.get((src, dst), [])
            if src == dst or not flist:
                cells_traffic.append(NOEXISTINGFLOW_TRAFFIC); cells_results.append(NOSIMRESULTS); cells_flow_results.append(NOSIMRESULTS)
                continue

            cells_traffic.append(":".join(encode_traffic_flow(f) for f in flist))
            fmetrics, all_delays = [], []
            for f in flist:
                fid = int(f["FlowId"])
                m = per_flow_metrics.get(fid)
                if m:
                    fmetrics.append(encode_result_metric(m)); all_delays.extend(m["delays"])
                    g_packets += m["pkts_gen"]; g_losses += m["pkts_drop"]
                else: fmetrics.append(NOSIMRESULTS)
            cells_flow_results.append(":".join(fmetrics))

            if all_delays:
                arr = np.array(all_delays)
                agg = {"avg_bw_kbps": sum(per_flow_metrics[int(f["FlowId"])]["avg_bw_kbps"] for f in flist),
                       "pkts_gen": sum(per_flow_metrics[int(f["FlowId"])]["pkts_gen"] for f in flist),
                       "pkts_drop": sum(per_flow_metrics[int(f["FlowId"])]["pkts_drop"] for f in flist),
                       "avg_delay": float(np.mean(arr)), "avg_ln_delay": float(np.mean(np.log(arr))),
                       "p10": float(np.percentile(arr,10)), "p20": float(np.percentile(arr,20)), "p50": float(np.percentile(arr,50)),
                       "p80": float(np.percentile(arr,80)), "p90": float(np.percentile(arr,90)), "jitter": float(np.var(arr))}
                g_delay_weighted += np.sum(arr); g_delay_count += len(arr)
                cells_results.append(encode_result_metric(agg))
            else: cells_results.append(NOSIMRESULTS)

    g_delay = g_delay_weighted / g_delay_count if g_delay_count > 0 else 0.0
    max_lambda = max(float(f["TimeDist"].get("EqLambda", 0.0)) for f in flows)
    return f"{max_lambda}|{';'.join(cells_traffic)}", f"{g_packets},{g_losses},{g_delay}|{';'.join(cells_results)}", ";".join(cells_flow_results)

# =========================
# SINGLE WORKER (Paralelizable)
# =========================
def process_single_tar(tar_path):
    try:
        with tarfile.open(tar_path, "r:gz") as tar:
            members = tar.getnames()
            base_name = members[0].split("/")[0]
            traffic_str = read_file_from_tar(tar, f"{base_name}/traffic.json")
            if not traffic_str: return None

            traffic_json = json.loads(traffic_str)
            flow_ids = [int(f["FlowId"]) for f in traffic_json["FeaturesperFlow"]]
            sim_time = float(traffic_json.get("SimulationTime"))

            parsed = parse_tx_rx_from_tar(tar, base_name, flow_ids)
            per_flow = {fid: flow_metrics(parsed[fid], sim_time) for fid in flow_ids}
            t_line, r_line, fr_line = build_lines(traffic_json, per_flow, sim_time)

            g_data = tar.extractfile(f"{base_name}/graph.gml").read()
            r_data = tar.extractfile(f"{base_name}/routing.txt").read()

            return {
                "base_name": base_name, 
                "t_line": t_line, "r_line": r_line, "fr_line": fr_line, 
                "g_data": g_data, "r_data": r_data, "sim_time": sim_time
            }
    except Exception as e:
        print(f"Error en {tar_path.name}: {e}")
        return None

# =========================
# WRITE / BATCH
# =========================
def write_batch_dir(tmp_dir, lines, folder_names, sim_time):
    tmp_dir.mkdir(parents=True, exist_ok=True)
    with open(tmp_dir/"traffic.txt","w") as ft, open(tmp_dir/"simulationResults.txt","w") as fr, \
         open(tmp_dir/"flowSimulationResults.txt","w") as ffr, open(tmp_dir/"stability.txt","w") as fs, \
         open(tmp_dir/"input_files.txt","w") as fi, open(tmp_dir/"descriptor.txt","w") as fd:
        for idx, item in enumerate(lines):
            ft.write(item["t_line"] + "\n"); fr.write(item["r_line"] + ";\n"); ffr.write(item["fr_line"] + ";\n")
            fs.write(f"{sim_time};OK;\n"); fi.write(f"{idx};{item['g_name']};{item['r_name']}\n"); fd.write(f"{idx};{folder_names[idx]}\n")

# =========================
# MAIN
# =========================
def build_dataset_from_tar(results_root, out_root, batch_name="batch_0000"):
    out_root.mkdir(parents=True, exist_ok=True)
    graphs_dir, routings_dir = out_root/"graphs", out_root/"routings"
    graphs_dir.mkdir(exist_ok=True); routings_dir.mkdir(exist_ok=True)

    tar_files = sorted(results_root.glob("*.tar.gz"))
    batch_lines, original_folders = [], []
    g_hash_map, r_hash_map = {}, {}
    g_count = r_count = 0
    last_sim_time = 0

    print(f"[INFO] Procesando {len(tar_files)} archivos con {MAX_PROCESSES} procesos...")

    with ProcessPoolExecutor(max_workers=MAX_PROCESSES) as executor:
        futures = {executor.submit(process_single_tar, tp): tp for tp in tar_files}
        
        for future in tqdm(as_completed(futures), total=len(futures), desc="Procesando"):
            res = future.result()
            if not res: continue

            original_folders.append(res["base_name"])
            last_sim_time = res["sim_time"]

            # Gestión de archivos (Hash para evitar duplicados en disco)
            g_hash = hashlib.md5(res["g_data"]).hexdigest()
            if g_hash not in g_hash_map:
                g_name = f"graph_{g_count}.gml"
                with open(graphs_dir/g_name, "wb") as f: f.write(res["g_data"])
                g_hash_map[g_hash] = g_name; g_count += 1
            
            r_hash = hashlib.md5(res["r_data"]).hexdigest()
            if r_hash not in r_hash_map:
                r_name = f"routing_{r_count}.txt"
                with open(routings_dir/r_name, "wb") as f: f.write(res["r_data"])
                r_hash_map[r_hash] = r_name; r_count += 1

            batch_lines.append({
                "t_line": res["t_line"], "r_line": res["r_line"], "fr_line": res["fr_line"], 
                "g_name": g_hash_map[g_hash], "r_name": r_hash_map[r_hash]
            })

    # Empaquetado final
    tmp = out_root / batch_name
    write_batch_dir(tmp, batch_lines, original_folders, last_sim_time)
    
    with tarfile.open(out_root/f"{batch_name}.tar.gz", "w:gz") as tar:
        tar.add(tmp, arcname=batch_name)
    shutil.rmtree(tmp)
    
    print(f"[OK] Generado {batch_name}.tar.gz con {len(batch_lines)} muestras.")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-root", default="./datasets/oran-hqos_merged/train")
    ap.add_argument("--out-root", default="../RouteNet-Fermi/final_set_datasetv2/oran-hqos/train")
    ap.add_argument("--batch-name", default="batch_0000")
    args = ap.parse_args()
    build_dataset_from_tar(Path(args.results_root), Path(args.out_root), args.batch_name)

if __name__ == "__main__":
    main()