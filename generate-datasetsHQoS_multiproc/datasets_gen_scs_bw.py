from gen_all_cfgs_zip import *
from traffic_configuration import *
import itertools
import random


period_scs_mapping = {
    15: 5,
    30: 2.5,
    60: 1.25
}

def get_periodicity(scs):
    return period_scs_mapping.get(scs)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--ns3-path', type=str, default='../ns-allinone-3.39/ns-3.39')
    parser.add_argument('--sim-time', type=float, default=1)
    parser.add_argument('--max-workers', type=int, default=3)
    args = parser.parse_args()


  
    
    graphfileslocation = "./topologies/"
    graphbase = ["SPWRRGraph_50_50_ALL", "SPGraph_ALL", "SPWRRGraph_60_40_ALL", "SPWRRGraph_40_60_ALL", "SPWRRGraph_70_30_ALL", "SPWRRGraph_30_70_ALL", "SPWRRGraph_90_10_ALL", "SPWRRGraph_10_90_ALL"]
    gen = AutomatedTopDatasetGenerator(args.ns3_path, "./datasets/oran-hqos_merged/")

    
    all_scs = [30]
    all_bw = [5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100]
    utilization_levels = np.linspace(0.1, 0.95, 40)
    valid_combinations = []
    
    
  
    for graph_file in graphbase:
        for scs, bw, util in itertools.product(all_scs, all_bw, utilization_levels):
            if scs in gen.gen_phys.guard_bands and bw in gen.gen_phys.guard_bands[scs]:
                valid_combinations.append({
                    'scs': scs,
                    'bw': bw,
                    'util': util,
                    'graph': graph_file
                })
            else:
                pass


    random.seed(42) 
    random.shuffle(valid_combinations)

    
    total = len(valid_combinations)
    idx_train = int(total * 1)
    idx_val = int(total * 1)  

    print(f"--- Generación de Dataset O-RAN ---")
    print(f"Combinaciones compatibles encontradas: {total}")
    print(f"Train: {idx_train} | Val: {idx_val - idx_train} | Test: {total - idx_val}")
    print(f"-----------------------------------")

  
    for i, config in enumerate(valid_combinations):
      
        if i < idx_train:
            subset = "train"
        elif i < idx_val:
            subset = "validation"
        else:
            subset = "test"

        scs, bw, util, graph_file = config['scs'], config['bw'], config['util'], config['graph']
        
        
        rrc_cfg_base = {
            'periodicity_ms': get_periodicity(scs),
            'nrofDownlinkSlots': 3, 'nrofDownlinkSymbols': 12,
            'nrofUplinkSlots': 1, 'nrofUplinkSymbols': 2
        }
        
        print(f"\n[BATCH {i+1}/{total}] [{subset.upper()}] SCS: {scs}kHz | BW: {bw}MHz | Util: {util:.2f}")

        # Ejecución en ns-3
        gen.generate_batch_pararell(
            batch_id=i,
            max_workers=args.max_workers,
            sim_time=args.sim_time, 
            rrc_cfg=rrc_cfg_base, 
            scs=scs, 
            bw=bw, 
            link_utilization_factor=util,
            subset=subset,
            graphbase=graph_file
            
        )