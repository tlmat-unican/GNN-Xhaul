import math
import os
import subprocess
import argparse
import tarfile
import shutil
import json
import networkx as nx
import numpy as np
from pathlib import Path
import math
import random

# Importamos tu generador físico
from traffic_configuration import ScenarioGenerator

# Constantes de diseño
SENSING_T = 1
OFH_T = 2 
TDD = 1
FDD = 2    

bh_traffic_flows = {"URLLC-MC": {"datarate": 833.33*1e6, "pkt_size": 225}, "URLLC-DA": {"datarate": 833.33*1e6, "pkt_size": 1358},
                   "eMBB-BH": {"datarate": 833.33*1e6, "pkt_size": 64}, "mIoT": {"datarate": 150*1e6, "pkt_size": 699}}

# bh_traffic_flows = {"URLLC-MC": {"datarate": 8000, "pkt_size": 225}, "URLLC-DA": {"datarate": 8000, "pkt_size": 1358},
#                    "eMBB-BH": {"datarate": 833.33, "pkt_size": 64}, "mIoT": {"datarate": 150, "pkt_size": 699}}

class AutomatedTopDatasetGenerator:
    """Generador integral de datasets O-RAN para RouteNet-Fermi"""
    
    def __init__(self, ns3_path, output_base_dir):
        self.ns3_path = Path(ns3_path)
        # Rutas en scratch de ns-3 donde el simulador lee por defecto
        self.scratch_path = self.ns3_path / "scratch"
        self.output_base_dir = Path(output_base_dir)
        
        self.traffic_json_path = self.scratch_path / "traffic.json"
        self.graph_file = self.scratch_path / "graph-triang.gml"
        self.routing_file = self.scratch_path / "routing-triang.txt"
        
        self.output_base_dir.mkdir(parents=True, exist_ok=True)
        self.configs_qos_mapping = self.get_gnn_training_configs()
        self.gen_phys = ScenarioGenerator()

    # --- LÓGICA DE SIMULACIÓN ---

    def run_ns3_simulation(self, scenario_id, sim_time, scenario_dir):
        # Directorio final para el dataset de este escenario
        # output_dir = self.output_base_dir / f"scenario_{scenario_id:04d}"
        output_dir =  scenario_dir
        output_dir.mkdir(parents=True, exist_ok=True)
        
        # NS-3 escribe las trazas en su carpeta de ejecución o la definida en el script .cc
        ns3_output_dir = self.ns3_path / "ns3-automated-output"

        # IMPORTANTE: Construimos el comando. 
        # Asegúrate de que tu script automatedtop.cc acepte el argumento --outputDir
        cmd = [
            "./ns3", "run", "automatedtop", "--",
            f"--simulationTime={sim_time}",
            f"--graphFile={self.graph_file.absolute()}", # Pasamos la ruta absoluta
            f"--routingFile={self.routing_file.absolute()}", # Pasamos la ruta absoluta
            f"--outputDir={output_dir.absolute()}" # Pasamos la ruta absoluta
        ]
        
        print(f"\n[Scenario {scenario_id:04d}] Ejecutando ns-3...")
        try:
            # result = subprocess.run(cmd, cwd=self.ns3_path, 
            #                         stdout=subprocess.DEVNULL,text=True, timeout=600)
            result = subprocess.run(cmd, cwd=self.ns3_path, timeout=600, text=True, capture_output=True)
            if result.returncode != 0:
                print(f"Error en ns-3:\n{result.stderr}")
                return False, output_dir, ns3_output_dir
            return True, output_dir, ns3_output_dir
        except Exception as e:
            print(f"Fallo crítico: {e}")
            return False, output_dir, ns3_output_dir

    def norm_capacity(self, bps_calculados):
        """
        Normaliza la capacidad del enlace a valores 'estándar'.
        """
        mbps = bps_calculados / 1e6
        
        if mbps <= 0:
            return 10 * 1e6  # Mínimo 10 Mbps para evitar errores
        
        if mbps < 100:
            # Ejemplo: 75 -> 80
            resultado_mbps = math.ceil(mbps / 10) * 10
        elif mbps < 1000:
            # Ejemplo: 621 -> 700 | 999 -> 1000
            resultado_mbps = math.ceil(mbps / 100) * 100
        else:
            # Ejemplo: 1250 -> 1500
            resultado_mbps = math.ceil(mbps / 500) * 500
            
        return int(resultado_mbps * 1e6) # En bps para ns-3     


    def get_gnn_training_configs(self):
        """
        Configuraciones solicitadas por el usuario para entrenamiento de GNN.
        Jerarquía: Q0 (Max) > Q1 (Med) > Q2 (Min)
        """
        return [
            # --- 1. Permutaciones de Colas Distintas (3! = 6 casos) ---
            {"OFH": 0, "SENSING": 1, "BH": 2, "desc": "Q0_Q1_Q2"}, # OFH prio
            # {"OFH": 0, "SENSING": 2, "BH": 1, "desc": "Q0_Q2_Q1"},
            # {"OFH": 1, "SENSING": 0, "BH": 2, "desc": "Q1_Q0_Q2"}, # Sensing prio
            # {"OFH": 1, "SENSING": 2, "BH": 0, "desc": "Q1_Q2_Q0"},
            # {"OFH": 2, "SENSING": 0, "BH": 1, "desc": "Q2_Q0_Q1"}, # Sensing prio, OFH fondo
            # {"OFH": 2, "SENSING": 1, "BH": 0, "desc": "Q2_Q1_Q0"}, # BH prio

            # # --- 2. Configuraciones de Compartición (Dúos) ---
            # {"OFH": 0, "SENSING": 1, "BH": 1, "desc": "Q0_Q1_Q1"}, # OFH prio, Sens/BH compiten
            # {"OFH": 1, "SENSING": 1, "BH": 0, "desc": "Q1_Q1_Q0"}, # BH prio, OFH/Sens compiten
            # {"OFH": 1, "SENSING": 0, "BH": 1, "desc": "Q1_Q0_Q1"}, # Sens prio, OFH/BH compiten
            # {"OFH": 0, "SENSING": 1, "BH": 0, "desc": "Q0_Q1_Q0"}, # Sens no prio, OFH/BH compiten arriba
            
            # {"OFH": 0, "SENSING": 0, "BH": 0, "desc": "Q0_Q0_Q0"}, # Sin distinciones 
        ]

    
    def generate_batch(self, batch_id=0, sim_time = 0.0001, rrc_cfg=None, bw=40, scs = 30, link_utilization_factor=0.85, subset="train", location="./topologies/" ,graphbase="SPGraph"):
        # Configuración base para este batch
     
        rrc_cfg = self.gen_phys.get_standard_rrc_config(scs) if rrc_cfg is None else rrc_cfg
        total_sims = len(self.configs_qos_mapping) * (rrc_cfg['nrofDownlinkSlots'] + 1)
        i = 1
     
        for config in self.configs_qos_mapping:
            # for slots_sensing in range(1, rrc_cfg['nrofDownlinkSlots'] + 2): # DL slots + flex symbol
            for slots_sensing in range(rrc_cfg['nrofDownlinkSlots'] + 1, rrc_cfg['nrofDownlinkSlots'] + 2):
                rrc_cfg_sensing = rrc_cfg.copy()
                if slots_sensing - 1 == rrc_cfg['nrofDownlinkSlots']:
                    rrc_cfg_sensing['nrofDownlinkSlots'] = slots_sensing - 1 
                    rrc_cfg_sensing['nrofDownlinkSymbols'] = rrc_cfg['nrofDownlinkSymbols'] 
                else:
                    rrc_cfg_sensing['nrofDownlinkSlots'] = slots_sensing
                    rrc_cfg_sensing['nrofDownlinkSymbols'] = 0
                    rrc_cfg_sensing['nrofUplinkSymbols'] = 14
                rrc_cfg_sensing['nrofUplinkSlots'] = rrc_cfg['nrofDownlinkSlots'] + rrc_cfg['nrofUplinkSlots'] - rrc_cfg_sensing['nrofDownlinkSlots']
                # print("Sensing config:")
                # print(rrc_cfg_sensing, "\n")
                flows_list = []
                port = 5000
                
                # Directorio actual del escenario
                
                
                flows_list, datarate_tdd = self.gen_phys.add_flow(flows_list, scs, bw, rrc_cfg, flow_type=OFH_T, port=port, source =4, destination=3,dscp=config["OFH"], DD=TDD)
                flows_list, _ = self.gen_phys.add_flow(flows_list, scs, bw, rrc_cfg_sensing, flow_type=SENSING_T, port=port +1,  source =4, destination=3, dscp=config["SENSING"], DD=TDD)
                flows_list, datarate_fdd = self.gen_phys.add_flow(flows_list, 15, 20, rrc_cfg, flow_type=OFH_T, port=port+2,  source =4, destination=3,dscp=config["OFH"], DD=FDD)
                flows_list, datarate_bh = self.gen_phys.add_bh_flows(flows_list, bh_traffic_flows, source=0, destination=3, port=port+3, dscp=config["BH"], flow_type="URLLC-DA")
                # flows_list = self.gen_phys.add_bh_flows(flows_list, bh_traffic_flows, source=0, destination=3, port=6000, dscp=config["BH"], flow_type="URLLC-MC")
                dscp_to_queue = {
                    config["OFH"]: config["OFH"],
                    config["SENSING"]: config["SENSING"],
                    config["BH"]: config["BH"]
                }
                
                total_demand = datarate_tdd + datarate_fdd + datarate_bh
                theo_cap = total_demand  / link_utilization_factor
                capacidad_final =  np.ceil(theo_cap)
                # capacidad_final = self.norm_capacity(theo_cap)
                # if capacidad_final < total_demand:
                #     capacidad_final += (1 * 1e6)
                
                # print("Utilization level: ", total_demand/capacidad_final)
                scenario_dir = self.output_base_dir / subset /f"graph_{graphbase}_scenario_{config['desc']}_SENSING{slots_sensing}_SCS{scs}_BW{bw}_UTIL{link_utilization_factor*100:.4f}"
                scenario_dir.mkdir(parents=True, exist_ok=True)
                G = nx.read_gml(location + graphbase + ".gml")
                for u, v, data in G.edges(data=True):
                    data['bandwidth'] = f"{capacidad_final}" 
                    
                # Sobrescribimos el archivo en scratch para que ns-3 lo use
                nx.write_gml(G, self.graph_file)
                # ---------------------------------------
                map_q = " ".join(f"{dscp} {queue}" for dscp, queue in dscp_to_queue.items())
                mark_q = " ".join(f"{f['Port']} {f['DSCP']}" for f in flows_list)

                # 3. Guardar traffic.json en SCRATCH (para que ns-3 lo lea)
                traffic_data = {
                    "NoF": len(flows_list),
                    "NoN": 5,
                    "SimulationTime": sim_time,
                    "MapQueue": map_q,
                    "MarkingPortQueue": mark_q,
                    "FeaturesperFlow": flows_list
                }
                
                with open(self.traffic_json_path, "w") as f:
                    json.dump(traffic_data, f, indent=2)

              
                shutil.copy(self.traffic_json_path, scenario_dir / "traffic.json")
                shutil.copy(self.graph_file, scenario_dir / "graph.gml")
                shutil.copy(self.routing_file, scenario_dir / "routing.txt")
                # print(f"\n Running simulation {i}/{total_sims}")
                # 5. Ejecutar Simulación
                success, out_dir, ns3_out = self.run_ns3_simulation(batch_id*total_sims + i, sim_time, scenario_dir)
                
                if success:
                    # print(f" -> Escenario {i} completado. Configuración y resultados en: {out_dir}")
                    print(f" -> Escenario {i} completado.")
                    # Aquí podrías llamar a parse_tx_rx_files y create_datanet_files
                else:
                    print(f" X Escenario {i} falló.")
                i+=1
    


   
