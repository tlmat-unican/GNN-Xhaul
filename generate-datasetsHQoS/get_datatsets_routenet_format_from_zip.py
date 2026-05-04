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
# PARSER TX/RX DESDE TAR
# =========================
def parse_tx_rx_from_tar(tar, base_name, flow_ids):
    flows = {}

    for fid in flow_ids:
        tx = {}
        rx = {}

        tx_name = f"{base_name}/txfileflow{fid}.txt"
        rx_name = f"{base_name}/rxfileflow{fid}.txt"

        tx_content = read_file_from_tar(tar, tx_name)
        rx_content = read_file_from_tar(tar, rx_name)

        if tx_content:
            for line in tx_content.splitlines():
                p = line.strip().split()
                if len(p) >= 3:
                    pkt, t_fs, sz = int(p[0]), float(p[1]), int(p[2])
                    tx[pkt] = (t_fs, sz)

        if rx_content:
            for line in rx_content.splitlines():
                p = line.strip().split()
                if len(p) >= 3:
                    pkt, t_fs, sz = int(p[0]), float(p[1]), int(p[2])
                    rx[pkt] = (t_fs, sz)

        flows[fid] = {"tx": tx, "rx": rx}

    return flows

# =========================
# MÉTRICAS
# =========================
def flow_metrics(flow_data, sim_time):
    tx = flow_data["tx"]
    rx = flow_data["rx"]

    delays = []
    sizes = []

    for pkt, (rt_fs, rsz) in rx.items():
        if pkt in tx:
            tt_fs, _ = tx[pkt]
            d = (rt_fs - tt_fs) / 1e9
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
        "avg_bw_kbps": avg_bw_kbps,
        "pkts_gen": pkts_gen,
        "pkts_drop": pkts_drop,
        "avg_delay": avg_delay,
        "avg_ln_delay": avg_ln_delay,
        "p10": p10, "p20": p20, "p50": p50, "p80": p80, "p90": p90,
        "jitter": jitter,
        "delays": delays
    }

# =========================
# ENCODERS
# =========================
def encode_traffic_flow(flow):
    tdist = flow["TimeDist"]
    sdist = flow["SizeDist"]
    tos = flow["DSCP"]

    ttype = tdist["Type"]
    tcode = TIME_DIST_CODE[ttype]

    if ttype == "ONOFF_T":
        time_part = f"{tcode},{tdist['EqLambda']},{tdist['pktsLambdaOn']},{tdist['avgTOff']},{tdist['avgTOn']},{tdist.get('ExpMaxFactor',1.0)}"
    elif ttype == "EXPONENTIAL_T":
        time_part = f"{tcode},{tdist['EqLambda']},{tdist['AvgPktsLambda']},{tdist.get('ExpMaxFactor',1.0)}"
    elif ttype == "DETERMINISTIC_T":
        time_part = f"{tcode},{tdist['EqLambda']},{tdist['AvgPktsLambda']}"
    else:
        raise ValueError(f"TimeDist no soportada: {ttype}")

    if sdist["Type"] != "DETERMINISTIC_S":
        raise ValueError("SizeDist no soportada")

    size_part = f"0,{sdist['AvgPktSize']}"
    return f"{time_part},{size_part},{tos}"

def encode_result_metric(m):
    return ",".join(map(str, [
        m['avg_bw_kbps'], m['pkts_gen'], m['pkts_drop'],
        m['avg_delay'], m['avg_ln_delay'],
        m['p10'], m['p20'], m['p50'], m['p80'], m['p90'],
        m['jitter']
    ]))

# =========================
# BUILD LINES
# =========================
def build_lines(traffic_json, per_flow_metrics, sim_time):
    n = int(traffic_json["NoN"])
    flows = traffic_json["FeaturesperFlow"]

    by_pair = {}
    for f in flows:
        key = (int(f["SourceNode"]), int(f["DestinationNode"]))
        by_pair.setdefault(key, []).append(f)

    cells_traffic, cells_results, cells_flow_results = [], [], []

    global_packets = global_losses = global_delay_weighted = 0.0
    global_delay_count = 0

    for src in range(n):
        for dst in range(n):
            if src == dst:
                cells_traffic.append(NOEXISTINGFLOW_TRAFFIC)
                cells_results.append(NOSIMRESULTS)
                cells_flow_results.append(NOSIMRESULTS)
                continue

            flist = by_pair.get((src, dst), [])
            if not flist:
                cells_traffic.append(NOEXISTINGFLOW_TRAFFIC)
                cells_results.append(NOSIMRESULTS)
                cells_flow_results.append(NOSIMRESULTS)
                continue

            cells_traffic.append(":".join(encode_traffic_flow(f) for f in flist))

            fmetrics = []
            all_delays = []

            for f in flist:
                fid = int(f["FlowId"])
                m = per_flow_metrics.get(fid)
                if m:
                    fmetrics.append(encode_result_metric(m))
                    all_delays.extend(m["delays"])
                    global_packets += m["pkts_gen"]
                    global_losses += m["pkts_drop"]
                else:
                    fmetrics.append(NOSIMRESULTS)

            cells_flow_results.append(":".join(fmetrics))

            if all_delays:
                arr = np.array(all_delays)
                agg = {
                    "avg_bw_kbps": sum(per_flow_metrics[int(f["FlowId"])]["avg_bw_kbps"] for f in flist),
                    "pkts_gen": sum(per_flow_metrics[int(f["FlowId"])]["pkts_gen"] for f in flist),
                    "pkts_drop": sum(per_flow_metrics[int(f["FlowId"])]["pkts_drop"] for f in flist),
                    "avg_delay": float(np.mean(arr)),
                    "avg_ln_delay": float(np.mean(np.log(arr))),
                    "p10": float(np.percentile(arr,10)),
                    "p20": float(np.percentile(arr,20)),
                    "p50": float(np.percentile(arr,50)),
                    "p80": float(np.percentile(arr,80)),
                    "p90": float(np.percentile(arr,90)),
                    "jitter": float(np.var(arr))
                }
                global_delay_weighted += np.sum(arr)
                global_delay_count += len(arr)
                cells_results.append(encode_result_metric(agg))
            else:
                cells_results.append(NOSIMRESULTS)

    global_delay = global_delay_weighted / global_delay_count if global_delay_count > 0 else 0.0
    max_lambda = max(float(f["TimeDist"].get("EqLambda", 0.0)) for f in flows)

    traffic_line = f"{max_lambda}|{';'.join(cells_traffic)}"
    results_line = f"{global_packets},{global_losses},{global_delay}|{';'.join(cells_results)}"
    flow_results_line = ";".join(cells_flow_results)

    return traffic_line, results_line, flow_results_line

# =========================
# WRITE
# =========================
def write_batch_dir(tmp_batch_dir, lines, folder_names, sim_time):
    tmp_batch_dir.mkdir(parents=True, exist_ok=True)

    with open(tmp_batch_dir / "traffic.txt","w") as ft, \
         open(tmp_batch_dir / "simulationResults.txt","w") as fr, \
         open(tmp_batch_dir / "flowSimulationResults.txt","w") as ffr, \
         open(tmp_batch_dir / "stability.txt","w") as fs, \
         open(tmp_batch_dir / "input_files.txt","w") as fi, \
         open(tmp_batch_dir / "descriptor.txt","w") as fd:

        for idx, item in enumerate(lines):
            ft.write(item["traffic_line"] + "\n")
            fr.write(item["results_line"] + ";\n")
            ffr.write(item["flow_results_line"] + ";\n")
            fs.write(f"{sim_time};OK;\n")
            fi.write(f"{idx};{item['graph_name']};{item['routing_name']}\n")
            fd.write(f"{idx};{folder_names[idx]}\n")

# =========================
# BUILD DATASET DESDE TAR
# =========================
def build_dataset_from_tar(results_root, out_root, batch_name="batch_0000"):
    out_root.mkdir(parents=True, exist_ok=True)

    graphs_dir = out_root / "graphs"
    routings_dir = out_root / "routings"
    graphs_dir.mkdir(exist_ok=True)
    routings_dir.mkdir(exist_ok=True)

    tar_files = sorted(results_root.glob("*.tar.gz"))

    # FILTRAR POR UTIL > 80
    filtered_tars = []

    for tar_path in tar_files:
        match = re.search(r"_UTIL(\d+(?:\.\d+)?)", tar_path.name)
        if match:
            util_value = float(match.group(1))
            if util_value < 0:
                filtered_tars.append(tar_path)

    print(f"[INFO] TARs originales: {len(tar_files)}")
    print(f"[INFO] TARs filtrados (UTIL<80): {len(filtered_tars)}")

    tar_files = filtered_tars if filtered_tars else tar_files



    # tar_files = sorted(results_root.glob("*.tar.gz"))
    # print(f"[INFO] TARs encontrados: {len(tar_files)}")

    graph_hash_map = {}
    routing_hash_map = {}
    graph_counter = routing_counter = 0

    batch_lines = []
    original_folders = []

    for tar_path in tqdm(tar_files, desc="Procesando TARs"):
        # print(f"[INFO] Procesando {tar_path.name}")

        with tarfile.open(tar_path, "r:gz") as tar:
            members = tar.getnames()
            base_name = members[0].split("/")[0]
            original_folders.append(base_name)

            traffic_str = read_file_from_tar(tar, f"{base_name}/traffic.json")
            if not traffic_str:
                continue

            traffic_json = json.loads(traffic_str)
            flow_ids = [int(f["FlowId"]) for f in traffic_json["FeaturesperFlow"]]
            sim_time = float(traffic_json.get("SimulationTime"))

            parsed = parse_tx_rx_from_tar(tar, base_name, flow_ids)
            per_flow = {fid: flow_metrics(parsed[fid], sim_time) for fid in flow_ids}

            traffic_line, results_line, flow_results_line = build_lines(
                traffic_json, per_flow, sim_time
            )

            # GRAPH
            g_data = tar.extractfile(f"{base_name}/graph.gml").read()
            g_hash = hashlib.md5(g_data).hexdigest()
            if g_hash not in graph_hash_map:
                name = f"graph_{graph_counter}.gml"
                with open(graphs_dir / name, "wb") as f:
                    f.write(g_data)
                graph_hash_map[g_hash] = name
                graph_counter += 1

            # ROUTING
            r_data = tar.extractfile(f"{base_name}/routing.txt").read()
            r_hash = hashlib.md5(r_data).hexdigest()
            if r_hash not in routing_hash_map:
                name = f"routing_{routing_counter}.txt"
                with open(routings_dir / name, "wb") as f:
                    f.write(r_data)
                routing_hash_map[r_hash] = name
                routing_counter += 1

            batch_lines.append({
                "traffic_line": traffic_line,
                "results_line": results_line,
                "flow_results_line": flow_results_line,
                "stable": True,
                "graph_name": graph_hash_map[g_hash],
                "routing_name": routing_hash_map[r_hash]
            })

    tmp = out_root / batch_name
    if tmp.exists():
        shutil.rmtree(tmp)

    write_batch_dir(tmp, batch_lines, original_folders, sim_time)

    tar_out = out_root / f"{batch_name}.tar.gz"
    with tarfile.open(tar_out, "w:gz") as tar:
        tar.add(tmp, arcname=batch_name)

    shutil.rmtree(tmp)

    print(f"[OK] {tar_out.name} con {len(batch_lines)} muestras")

# =========================
# MAIN
# =========================
def main():
    ap = argparse.ArgumentParser()
    # ap.add_argument("--results-root", default="./datasets/oran-hqos_merged/train")
    ap.add_argument("--results-root", default="./datasets/oran-hqos_merged/validation")
    ap.add_argument("--out-root", default="../RouteNet-Fermi/final_set_dataset/oran-hqos_merged_ALL/validation")
    ap.add_argument("--batch-name", default="batch_0000")
    args = ap.parse_args()

    build_dataset_from_tar(
        Path(args.results_root),
        Path(args.out_root),
        args.batch_name
    )

if __name__ == "__main__":
    main()