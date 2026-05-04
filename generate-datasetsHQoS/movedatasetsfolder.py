import os
import shutil
import random

# CONFIGURACIÓN
SOURCE_DIR = "./datasets/oran-hqos_merged/validation" # O 'train'
TARGET_DIR = "./datasets/oran-hqos_merged/train" # O 'validation'

PERCENTAGE_TO_MOVE = 0.2 # 40% para validation




def count_folders(path):
    return len([
        d for d in os.listdir(path)
        if os.path.isdir(os.path.join(path, d))
    ])




target_count = count_folders(TARGET_DIR)
source_count = count_folders(SOURCE_DIR)

print(f"Carpetas en TARGET ({TARGET_DIR}): {target_count}")
print(f"Carpetas en SOURCE ({SOURCE_DIR}): {source_count}")

# # Crear carpeta destino si no existe
# os.makedirs(TARGET_DIR, exist_ok=True)

# # Obtener lista de todos los escenarios en train
# scenarios = [d for d in os.listdir(SOURCE_DIR) if os.path.isdir(os.path.join(SOURCE_DIR, d))]
# scenarios.sort() # Los ordenamos para tener control

# # Seleccionar cuáles mover
# # Opción A: Aleatorio
# num_to_move = int(len(scenarios) * PERCENTAGE_TO_MOVE)
# to_move = random.sample(scenarios, num_to_move)

# # Opción B: Sistemático (ej: cada 5 carpetas para cubrir todo el rango UTIL)
# # to_move = scenarios[::5] 

# print(f"Moviendo {len(to_move)} escenarios de {len(scenarios)} totales...")

# for folder in to_move:
#     src = os.path.join(SOURCE_DIR, folder)
#     dst = os.path.join(TARGET_DIR, folder)
    
#     # Usamos shutil.move para quitarlos de train y ponerlos en test
#     shutil.move(src, dst)
#     print(f" [✓] Movido: {folder}")

# print("\n¡Listo! Tu dataset está dividido correctamente.")