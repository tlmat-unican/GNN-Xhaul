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
import tarfile
import concurrent.futures
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
        
    
 

    def run_ns3_simulation_zip(self, scenario_id, sim_time, scenario_dir):
        # Directorio final para el dataset de este escenario
        output_dir = scenario_dir
        output_dir.mkdir(parents=True, exist_ok=True)
        
        ns3_output_dir = self.ns3_path / "ns3-automated-output"

        cmd = [
            "./ns3", "run", "automatedtop", "--",
            f"--simulationTime={sim_time}",
            f"--graphFile={self.graph_file.absolute()}",
            f"--routingFile={self.routing_file.absolute()}",
            f"--outputDir={output_dir.absolute()}"
        ]
        
        print(f"\n[Scenario {scenario_id:04d}] Ejecutando ns-3...")

        try:
            result = subprocess.run(
                cmd,
                cwd=self.ns3_path,
                timeout=600,
                text=True,
                capture_output=True
            )

            if result.returncode != 0:
                print(f"Error en ns-3:\n{result.stderr}")
                return False, output_dir, ns3_output_dir

            # =========================================================
            # COMPRESIÓN TAR.GZ
            # =========================================================
            tar_path = output_dir.with_suffix(".tar.gz")

            print(f"[Scenario {scenario_id:04d}] Comprimiendo resultados en {tar_path}...")

            with tarfile.open(tar_path, "w:gz") as tar:
                tar.add(output_dir, arcname=output_dir.name)

            print(f"[Scenario {scenario_id:04d}] Compresión completada")
            shutil.rmtree(output_dir)

            print(f"[Scenario {scenario_id:04d}] Directorio eliminado")
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
            {"OFH": 0, "SENSING": 2, "BH": 1, "desc": "Q0_Q2_Q1"},
            {"OFH": 1, "SENSING": 0, "BH": 2, "desc": "Q1_Q0_Q2"}, # Sensing prio
            {"OFH": 1, "SENSING": 2, "BH": 0, "desc": "Q1_Q2_Q0"},
            {"OFH": 2, "SENSING": 0, "BH": 1, "desc": "Q2_Q0_Q1"}, # Sensing prio, OFH fondo
            {"OFH": 2, "SENSING": 1, "BH": 0, "desc": "Q2_Q1_Q0"}, # BH prio

            # --- 2. Configuraciones de Compartición (Dúos) ---
            {"OFH": 0, "SENSING": 1, "BH": 1, "desc": "Q0_Q1_Q1"}, # OFH prio, Sens/BH compiten
            {"OFH": 1, "SENSING": 1, "BH": 0, "desc": "Q1_Q1_Q0"}, # BH prio, OFH/Sens compiten
            {"OFH": 1, "SENSING": 0, "BH": 1, "desc": "Q1_Q0_Q1"}, # Sens prio, OFH/BH compiten
            {"OFH": 0, "SENSING": 1, "BH": 0, "desc": "Q0_Q1_Q0"}, # Sens no prio, OFH/BH compiten arriba
            
            {"OFH": 0, "SENSING": 0, "BH": 0, "desc": "Q0_Q0_Q0"}, # Sin distinciones 
        ]

    
    
    
    @staticmethod
    def _run_single_sim_worker(task):
        """
        Función estática para ser ejecutada por los procesos hijos.
        No comparte memoria con el padre para evitar conflictos.
        """
        ns3_path = task['ns3_path']
        scenario_id = task['scenario_id']
        scenario_dir = task['scenario_dir']
        sim_time = task['sim_time']
        traffic_data = task['traffic_data']
        graph_obj = task['graph_obj']
        routing_src = task['routing_src']

        try:
            # 1. Crear directorio del escenario
            scenario_dir.mkdir(parents=True, exist_ok=True)
            
            # 2. Rutas de archivos LOCALES para que no choquen entre procesos
            local_traffic = scenario_dir / "traffic.json"
            local_graph = scenario_dir / "graph.gml"
            local_routing = scenario_dir / "routing.txt"

            # 3. Guardar archivos de entrada específicos
            with open(local_traffic, "w") as f:
                json.dump(traffic_data, f, indent=2)
            nx.write_gml(graph_obj, local_graph)
            if routing_src.exists():
                shutil.copy(routing_src, local_routing)

            # 4. Construir comando ns-3 apuntando a archivos locales
            cmd = [
                "./ns3", "run", "automatedtop", "--",
                f"--simulationTime={sim_time}",
                f"--graphFile={local_graph.absolute()}",
                f"--trafficFile={local_traffic.absolute()}",
                f"--routingFile={local_routing.absolute()}",
                f"--outputDir={scenario_dir.absolute()}"
            ]

            # 5. Ejecutar ns-3
            result = subprocess.run(
                cmd, cwd=ns3_path, timeout=900, 
                text=True, capture_output=True
            )

            if result.returncode != 0:
                return False, scenario_id, f"Error ns-3: {result.stderr[:200]}"

            # 6. Compresión y limpieza para ahorrar espacio
            tar_path = scenario_dir.with_suffix(".tar.gz")
            with tarfile.open(tar_path, "w:gz") as tar:
                tar.add(scenario_dir, arcname=scenario_dir.name)
            
            shutil.rmtree(scenario_dir)
            return True, scenario_id, "Éxito"

        except Exception as e:
            return False, scenario_id, str(e)
    
    
    
    
    
    
    
    
    def generate_batch_pararell(self, max_workers=3, batch_id=0, sim_time=0.0001, rrc_cfg=None, bw=40, scs=30, link_utilization_factor=0.85, subset="train", location="./topologies/", graphbase="SPGraph"):
        
        rrc_cfg = self.gen_phys.get_standard_rrc_config(scs) if rrc_cfg is None else rrc_cfg
        sim_count = 1
        tasks = []
        

        routing_src = Path(location) / "routing-triang.txt"
        
        print(f"--- Preparando configuraciones para el batch {batch_id} ---")
        
        # PRIMERA PARTE: Solo llenar la lista de tareas
        for config in self.configs_qos_mapping:
            for slots_sensing in range(1, rrc_cfg['nrofDownlinkSlots'] + 2):
                rrc_cfg_sensing = rrc_cfg.copy()
                
                
                if slots_sensing - 1 == rrc_cfg['nrofDownlinkSlots']:
                    rrc_cfg_sensing['nrofDownlinkSlots'] = slots_sensing - 1 
                    rrc_cfg_sensing['nrofDownlinkSymbols'] = rrc_cfg['nrofDownlinkSymbols'] 
                else:
                    rrc_cfg_sensing['nrofDownlinkSlots'] = slots_sensing
                    rrc_cfg_sensing['nrofDownlinkSymbols'] = 0
                    rrc_cfg_sensing['nrofUplinkSymbols'] = 14
                rrc_cfg_sensing['nrofUplinkSlots'] = rrc_cfg['nrofDownlinkSlots'] + rrc_cfg['nrofUplinkSlots'] - rrc_cfg_sensing['nrofDownlinkSlots']

                flows_list = []
                port = 5000
                
                # Generar flujos
                flows_list, datarate_tdd = self.gen_phys.add_flow(flows_list, scs, bw, rrc_cfg, flow_type=OFH_T, port=port, source=4, destination=3, dscp=config["OFH"], DD=TDD)
                flows_list, _ = self.gen_phys.add_flow(flows_list, scs, bw, rrc_cfg_sensing, flow_type=SENSING_T, port=port + 1, source=4, destination=3, dscp=config["SENSING"], DD=TDD)
                flows_list, datarate_fdd = self.gen_phys.add_flow(flows_list, 15, 20, rrc_cfg, flow_type=OFH_T, port=port + 2, source=4, destination=3, dscp=config["OFH"], DD=FDD)
                flows_list, datarate_bh = self.gen_phys.add_bh_flows(flows_list, bh_traffic_flows, source=0, destination=3, port=port + 3, dscp=config["BH"], flow_type="URLLC-DA")
                
                total_demand = datarate_tdd + datarate_fdd + datarate_bh
                capacidad_final = np.ceil(total_demand / link_utilization_factor)
                
                # Configurar Grafo
                G = nx.read_gml(location + graphbase + ".gml")
                for u, v, data in G.edges(data=True):
                    data['bandwidth'] = f"{capacidad_final}" 

                # Datos del tráfico para el JSON
                map_q = " ".join(f"{dscp} {queue}" for dscp, queue in {config["OFH"]: config["OFH"], config["SENSING"]: config["SENSING"], config["BH"]: config["BH"]}.items())
                mark_q = " ".join(f"{f['Port']} {f['DSCP']}" for f in flows_list)

                traffic_data = {
                    "NoF": len(flows_list),
                    "NoN": 5,
                    "SimulationTime": sim_time,
                    "MapQueue": map_q,
                    "MarkingPortQueue": mark_q,
                    "FeaturesperFlow": flows_list
                }

                scenario_name = f"graph_{graphbase}_scenario_{config['desc']}_SENSING{slots_sensing}_SCS{scs}_BW{bw}_UTIL{link_utilization_factor*100:.4f}"
                scenario_dir = self.output_base_dir / subset / scenario_name
                
                # Añadimos la configuración a la lista de tareas
                task = {
                    'ns3_path': self.ns3_path,
                    'scenario_id': batch_id * 1000 + sim_count,
                    'scenario_dir': scenario_dir,
                    'sim_time': sim_time,
                    'traffic_data': traffic_data,
                    'graph_obj': G,
                    'routing_src': routing_src
                }
                tasks.append(task)
                sim_count += 1

        # SEGUNDA PARTE: Ejecución en paralelo 
        print(f"Lanzando {len(tasks)} simulaciones en {max_workers} procesos...")
        
        with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
            # Enviamos todas las tareas acumuladas en 'tasks'
            future_to_id = {executor.submit(self._run_single_sim_worker, t): t['scenario_id'] for t in tasks}
            
            # Procesamos resultados conforme terminen
            for i, future in enumerate(concurrent.futures.as_completed(future_to_id), 1):
                success, s_id, msg = future.result()
                status = "OK" if success else "ERROR"
                print(f"[{i}/{len(tasks)}] Escenario {s_id:04d}: {status} ({msg})")