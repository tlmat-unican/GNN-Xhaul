import os
import shutil
import random

# --- CONFIGURACIÓN ---
BASE_PATH = "./datasets/oran-hqos_merged"
SOURCE_DIR = os.path.join(BASE_PATH, "train")
VAL_DIR = os.path.join(BASE_PATH, "validation")
TEST_DIR = os.path.join(BASE_PATH, "test")

# Definimos los porcentajes respecto al total original
P_VAL = 0.10  # 10%
P_TEST = 0.10 # 10%
EXTENSIONS = (".tar", ".tar.gz", ".tgz")

# Asegurar que las carpetas de destino existan
os.makedirs(VAL_DIR, exist_ok=True)
os.makedirs(TEST_DIR, exist_ok=True)

def get_file_list(path):
    return [
        f for f in os.listdir(path)
        if f.endswith(EXTENSIONS) and os.path.isfile(os.path.join(path, f))
    ]

# 1. Obtener todos los archivos actuales en train
all_files = get_file_list(SOURCE_DIR)
all_files.sort() # Para consistencia antes del shuffle
random.seed(42)  # Opcional: para que el split sea reproducible
random.shuffle(all_files)

total_count = len(all_files)
print(f"Archivos encontrados en '{SOURCE_DIR}': {total_count}")

# if total_count == 0:
#     print("No se encontraron archivos en la carpeta de origen.")
# else:
#     # 2. Calcular cantidades
#     num_val = int(total_count * P_VAL)
#     num_test = int(total_count * P_TEST)

#     val_files = all_files[:num_val]
#     test_files = all_files[num_val : num_val + num_test]
    
#     print(f"Total de archivos detectados: {total_count}")
#     print(f"Moviendo {len(val_files)} a VAL y {len(test_files)} a TEST...")

#     # 4. Función auxiliar para mover
#     def move_files(files, destination):
#         for file_name in files:
#             src = os.path.join(SOURCE_DIR, file_name)
#             dst = os.path.join(destination, file_name)
#             try:
#                 shutil.move(src, dst)
#             except Exception as e:
#                 print(f" [X] Error al mover {file_name}: {e}")

#     # Ejecutar movimientos
#     move_files(val_files, VAL_DIR)
#     move_files(test_files, TEST_DIR)

#     # 5. Conteo final de verificación
#     print("\n--- Distribución Final ---")
#     print(f"TRAIN: {len(get_file_list(SOURCE_DIR))} (aprox 80%)")
#     print(f"VAL:   {len(get_file_list(VAL_DIR))} (10%)")
#     print(f"TEST:  {len(get_file_list(TEST_DIR))} (10%)")