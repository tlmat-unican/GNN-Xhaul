import json
import math



# Constantes
SENSING_T = 1
OFH_T = 2 
TDD = 1
FDD = 2

class ScenarioGenerator:
    def __init__(self):
        # Datos de BW guard de tu tabla MATLAB (SCS: {BW: Guard})
        self.guard_bands = {
            15: {5: 242.5, 10: 312.5, 15: 382.5, 20: 452.5, 25: 522.5, 30: 592.5, 40: 552.5, 50: 692.5},
            30: {5: 505, 10: 665, 15: 645, 20: 805, 25: 785, 30: 945, 40: 905, 50: 1045, 60: 825, 70: 965, 80: 925, 90: 885, 100: 845},
            60: {10: 1010, 15: 990, 20: 1330, 25: 1310, 30: 1290, 40: 1610, 50: 1570, 60: 1530, 70: 1490, 80: 1450, 90: 1410, 100: 1370}
        }
        
        # Duración de símbolo por SCS
        self.t_sym_map = {15: 71.4e-6, 30: 35.7e-6, 60: 17.8e-6}
        
    def calculate_rrc_tdd_times(self, scs, rrc_cfg, direction="UL"):
        """
        Calcula T_on y T_off siguiendo el estándar TDD-UL-DL-Pattern de RRC.
        rrc_cfg: {
            'periodicity_ms': 5,
            'nrofDownlinkSlots': 7,
            'nrofDownlinkSymbols': 6,
            'nrofUplinkSlots': 2,
            'nrofUplinkSymbols': 4
        }
        """
       
        t_slot = 1e-3 / (scs / 15) # Obtain slot duration based on SCS (15kHz base)
        t_symbol = t_slot / 14 # Duration of one OFDM symbol (14 symbols per slot in normal CP)
        # print("t_slot:", t_slot, "t_symbol:", t_symbol)
        
        # Periodicity specidied in rrc_cfg is in ms, convert to seconds
        t_period = rrc_cfg['periodicity_ms'] * 1e-3
        # print("T_period:", t_period, "s, T_slot:", t_slot, "s, T_symbol:", f"{t_symbol*1e6:.2f}", "us", " Maximum slots:", t_period / t_slot)
      
        if t_period / t_slot == 1:
            t_slot = 0 # only using the symbols for the on time, as a Flex slot
        # Downlink Time (Slots + extra symbols (gAP))
        if rrc_cfg['nrofDownlinkSlots'] + rrc_cfg['nrofUplinkSlots'] + 1  < t_period / t_slot:
            print(f">> Slot dont fill the period; period {t_period}, slot {t_slot}, number slots per period: {t_period / t_slot}, config file has {rrc_cfg['nrofDownlinkSlots'] + rrc_cfg['nrofUplinkSlots']} !!!")

        
        t_dl = (rrc_cfg['nrofDownlinkSlots'] * t_slot) + (rrc_cfg['nrofDownlinkSymbols'] * t_symbol)
        
        # Uplink Time (Slots + extra symbols)
        t_ul = (rrc_cfg['nrofUplinkSlots'] * t_slot) + (rrc_cfg['nrofUplinkSymbols'] * t_symbol)

        # Definición de ON/OFF según el flujo que estemos modelando
        if direction == "UL":
            t_on = t_ul
            # El tiempo OFF es el resto del periodo (incluye DL y el GAP de conmutación)
            t_off = t_period - t_ul
        else: # Downlink
            t_on = t_dl
            t_off = t_period - t_dl

        # Validación para evitar valores negativos o cero por error de config
        return max(t_on, 1e-9), max(t_off, 1e-9)

    
    def get_ofh_features(self, scs, bw_mhz, rrc_cfg, direction="UL", DD=TDD):
        """Une la física de O-FH con la estructura TDD RRC"""
        # 1. Obtener parámetros de paquetes (Basado en tus tablas MATLAB/BFP9)
        guard = self.guard_bands[scs][bw_mhz]
        prbs = math.floor((bw_mhz * 1000 - (guard * 2)) / (scs * 12))
        # print("PRBs:", prbs)
        # Tamaño de paquete por símbolo (PRB * bytes_por_prb + header)
        prb_size_bytes = (9 * 12 * 2 + 8) / 8  # BFP9
        pkt_size = int(prbs * prb_size_bytes) + 36 # (Eth header (14) + VALN 891.Q (4) + eCPRI Common header (4) + eCPRI (14))
        # print("PRB(bytes):", prb_size_bytes)
        # 2. Tasa de paquetes (1 paquete por cada símbolo OFDM en tiempo ON)
        t_symbol = (1e-3 / (scs / 15)) / 14
        # t_symbol = 0.1
        pkts_lambda_on = 1 / t_symbol
        datarate = pkts_lambda_on * (pkt_size) * 8  # bits por segundo
        
        
        if DD == TDD:
            # 3. Tiempos ON/OFF basados en RRC
            t_on, t_off = self.calculate_rrc_tdd_times(scs, rrc_cfg, direction)
        else: 
            t_on = 1
            t_off = 0
                    
        if DD == TDD and (t_on*1e3 + t_off*1e3 != rrc_cfg["periodicity_ms"]):
            print("!! ON + OFF doesnt equal period for config period:", rrc_cfg["periodicity_ms"], "Calculated ON:", t_on, "OFF:", t_off, "Total:", (t_on + t_off)*1e3, "\n")
        # print(f"Scs: {scs} kHz, BW: {bw_mhz} MHz -> Packet Size: {pkt_size} bytes, "
        #     f"ON: {t_on*1e3:.2f} ms, OFF: {t_off*1e3:.2f} ms, Data Rate: {datarate/1e6:.2f} Mbps "
        #     f"{'TDD' if DD == TDD else 'FDD'} "
        #     f"{'SENSING' if direction == 'DL' else 'OFH'}")

        return {
            "Type": "ONOFF_T",
            "pktsLambdaOn": pkts_lambda_on,
            "avgTOn": round(t_on, 8),
            "avgTOff": round(t_off, 8),
            "AvgPktSize": pkt_size,
            "EqLambda": pkts_lambda_on * (t_on / (t_on + t_off)) * pkt_size,
        }, datarate
        
        
        
        
    def add_flow(self, flows, scs, bw, rrc_config, flow_type, port, source=0, destination=3, dscp=0, DD=TDD):

    
        # Determinar dirección según el tipo
        direction = "UL" if flow_type == OFH_T else "DL"
        # dscp = 0 if flow_type == OFH_T else 1 # DSCP 0 para OFH, 1 para Sensing
        start_in_on_state = True if flow_type == SENSING_T else False
        
        data, datarate = self.get_ofh_features(scs, bw, rrc_config, direction=direction, DD=DD)
        
        flows.append({
            "FlowId": len(flows) + 1,
            "SourceNode": source,
            "DestinationNode": destination,
            "DSCP": dscp,
            "Port": port,
        
            "TimeDist": {
                "Type": "ONOFF_T",
                "EqLambda": data["EqLambda"],
                "pktsLambdaOn": data["pktsLambdaOn"], # Convertir a pkts/ms para el formato de traffic.json
                "avgTOn": data["avgTOn"],
                "avgTOff": data["avgTOff"],
                "ExpMaxFactor": 10.0,
                "StartInOnState": start_in_on_state
            },
            "SizeDist": {
                "Type": "DETERMINISTIC_S",
                "AvgPktSize": data["AvgPktSize"] 
            }
        })
        return flows, datarate
    
    def add_bh_flows(self, flows, bh_traffic_flows, source, destination, port=5000, flow_type="URLLC-MC", dscp=2):
       
        params = bh_traffic_flows[flow_type]
        # Para BH, asumimos tráfico constante (puede ser modificado para ser más realista)
        flows.append({
            "FlowId": len(flows) + 1,
            "SourceNode": source,
            "DestinationNode": destination,
            "DSCP": dscp, # DSCP 2 para BH
            "Port": port,
            "TimeDist": {
                "Type": "EXPONENTIAL_T",
                "AvgPktsLambda":  ((params["datarate"])/ (params["pkt_size"] * 8)), # pkts/s
                "EqLambda":  ((params["datarate"]) / (8)),
                "ExpMaxFactor": 1.0
            },
            "SizeDist": {
                "Type": "DETERMINISTIC_S",
                "AvgPktSize": params["pkt_size"]
            }
        })
        return flows, params["datarate"]

    
    def get_standard_rrc_config(scs_khz):
        """
        Retorna la configuración RRC 'de facto' en la industria para cada SCS.
        """
        if scs_khz == 15:
            # Típico en bandas bajas (n28). Periodo largo.
            return {
                'periodicity_ms': 5, # Estándar para 15kHz
                'nrofDownlinkSlots': 3, 
                'nrofDownlinkSymbols': 10,
                'nrofUplinkSlots': 1,
                'nrofUplinkSymbols': 2
            }
        elif scs_khz == 30:
            # Típico en Banda Media (n78). Es tu caso principal.
            return {
                'periodicity_ms': 2.5, # El estándar de oro para 30kHz
                'nrofDownlinkSlots': 3,
                'nrofDownlinkSymbols': 12,
                'nrofUplinkSlots': 1,
                'nrofUplinkSymbols': 2
            }
        elif scs_khz == 60:
            # mmWave / Industrial IoT. Latencia ultra baja.
            return {
                'periodicity_ms': 1.25,
                'nrofDownlinkSlots': 3,
                'nrofDownlinkSymbols': 12,
                'nrofUplinkSlots': 1,
                'nrofUplinkSymbols': 2
            }
        else:
            raise ValueError("SCS no soportada para configuración por defecto.")