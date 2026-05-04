#!/usr/bin/env python3
import argparse
import json
import tarfile
from pathlib import Path
import shutil
import numpy as np
import hashlib

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
# HASH
# =========================
def file_hash(path: Path):
    h = hashlib.md5()
    with path.open('rb') as f:
        chunk = f.read(8192)
        while chunk:
            h.update(chunk)
            chunk = f.read(8192)
    return h.hexdigest()
# =========================
# PARSER TX/RX
# =========================
def parse_tx_rx_files(scenario_dir: Path, flow_ids):
    flows = {}
    for fid in flow_ids:
        txf = scenario_dir / f"txfileflow{fid}.txt"
        rxf = scenario_dir / f"rxfileflow{fid}.txt"
        tx = {}
        rx = {}

        if txf.exists():
            with txf.open() as f:
                for line in f:
                    p = line.strip().split()
                    if len(p) >= 3:
                        pkt, t_fs, sz = int(p[0]), float(p[1]), int(p[2])
                        tx[pkt] = (t_fs, sz)

        if rxf.exists():
            with rxf.open() as f:
                for line in f:
                    p = line.strip().split()
                    if len(p) >= 3:
                        pkt, t_fs, sz = int(p[0]), float(p[1]), int(p[2])
                        rx[pkt] = (t_fs, sz)

        flows[fid] = {"tx": tx, "rx": rx}
    return flows

# =========================
# MÉTRICAS POR FLUJO
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
# WRITE BATCH
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

def filtering_per_util(results_root, data_cluster):
    all_scenarios = sorted(results_root.glob(f"scenario_{data_cluster}*"))
    
    scenarios = []
    for sc in all_scenarios:
        try:
            # Extraemos el valor después de 'UTIL' en el nombre de la carpeta
            # Ejemplo: scenario_..._UTIL17.8947 -> 17.8947
            util_str = sc.name.split("UTIL")[-1]
            util_val = float(util_str)
            
            # FILTRO: Solo menores a 85
            if util_val < 85.0:
                scenarios.append(sc)
        except (ValueError, IndexError):
            # Si el nombre no tiene el formato esperado, puedes decidir si saltarlo o incluirlo
            print(f"[WARN] No se pudo determinar UTIL en {sc.name}. Saltando...")
            continue

    print(f"[INFO] Escenarios que cumplen UTIL < 85: {len(scenarios)} de {len(all_scenarios)}")
    return scenarios

# =========================
# BUILD DATASET
# =========================
def build_dataset(results_root, out_root, batch_name="batch_0000", data_cluster=None):
    out_root.mkdir(parents=True, exist_ok=True)
    graphs_dir = out_root / "graphs"
    routings_dir = out_root / "routings"
    graphs_dir.mkdir(exist_ok=True)
    routings_dir.mkdir(exist_ok=True)
    scenarios = sorted(results_root.glob(f"graph_{data_cluster}*"))
    if data_cluster:
        scenarios = filtering_per_util(results_root, data_cluster)
    print(f"[INFO] Total escenarios a procesar: {len(scenarios)}")
    original_folders = []
    graph_hash_map = {}
    routing_hash_map = {}
    graph_counter = routing_counter = 0

    batch_lines = []
    
    for sc in scenarios:
        original_folders.append(sc.name)
        traffic_json = json.loads((sc / "traffic.json").read_text())
        flow_ids = [int(f["FlowId"]) for f in traffic_json["FeaturesperFlow"]]
        sim_time = float(traffic_json.get("SimulationTime"))
    
        parsed = parse_tx_rx_files(sc, flow_ids)
        per_flow = {fid: flow_metrics(parsed[fid], sim_time) for fid in flow_ids}

        traffic_line, results_line, flow_results_line = build_lines(traffic_json, per_flow, sim_time)

        # GRAPH
        g_hash = file_hash(sc / "graph.gml")
        if g_hash not in graph_hash_map:
            name = f"graph_{graph_counter}.gml"
            shutil.copy(sc / "graph.gml", graphs_dir / name)
            graph_hash_map[g_hash] = name
            graph_counter += 1
        graph_name = graph_hash_map[g_hash]

        # ROUTING
        r_hash = file_hash(sc / "routing.txt")
        if r_hash not in routing_hash_map:
            name = f"routing_{routing_counter}.txt"
            shutil.copy(sc / "routing.txt", routings_dir / name)
            routing_hash_map[r_hash] = name
            routing_counter += 1
        routing_name = routing_hash_map[r_hash]

        batch_lines.append({
            "traffic_line": traffic_line,
            "results_line": results_line,
            "flow_results_line": flow_results_line,
            "stable": True,
            "graph_name": graph_name,
            "routing_name": routing_name
        })

    tmp = out_root / batch_name
    if tmp.exists():
        shutil.rmtree(tmp)

    write_batch_dir(tmp, batch_lines, original_folders, sim_time)

    tar_path = out_root / f"{batch_name}.tar.gz"
    with tarfile.open(tar_path, "w:gz") as tar:
        tar.add(tmp, arcname=batch_name)

    shutil.rmtree(tmp)

    print(f"[OK] {tar_path.name} con {len(batch_lines)} muestras")
    print(f"[INFO] Grafos únicos: {len(graph_hash_map)}")
    print(f"[INFO] Routings únicos: {len(routing_hash_map)}")

# =========================
# MAIN
# =========================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-root", default="./datasets/oran-hqos_test/test")
    ap.add_argument("--out-root", default="../RouteNet-Fermi/data/oran-hqos_test/validation")
    ap.add_argument("--batch-name", default="batch_0000")
    args = ap.parse_args()
   
    build_dataset(Path(args.results_root), Path(args.out_root), args.batch_name, data_cluster="")

if __name__ == "__main__":
    main()