import os
import shutil
import random

# --- Configuration ---
BASE_PATH = "./datasets/oran-hqos_merged"
SOURCE_DIR = os.path.join(BASE_PATH, "train")
VAL_DIR = os.path.join(BASE_PATH, "validation")
TEST_DIR = os.path.join(BASE_PATH, "test")

# Define the percentage for validation and test splits
P_VAL = 0.10  # 10%
P_TEST = 0.10 # 10%
EXTENSIONS = (".tar", ".tar.gz", ".tgz")


os.makedirs(VAL_DIR, exist_ok=True)
os.makedirs(TEST_DIR, exist_ok=True)

def get_file_list(path):
    return [
        f for f in os.listdir(path)
        if f.endswith(EXTENSIONS) and os.path.isfile(os.path.join(path, f))
    ]

# Get all files from the source directory
all_files = get_file_list(SOURCE_DIR)
all_files.sort() 
random.seed(42) 
random.shuffle(all_files)

total_count = len(all_files)
print(f"Archivos encontrados en '{SOURCE_DIR}': {total_count}")

if total_count == 0:
    print("No se encontraron archivos en la carpeta de origen.")
else:
    num_val = int(total_count * P_VAL)
    num_test = int(total_count * P_TEST)

    val_files = all_files[:num_val]
    test_files = all_files[num_val : num_val + num_test]
    
    print(f"Total de archivos detectados: {total_count}")
    print(f"Moviendo {len(val_files)} a VAL y {len(test_files)} a TEST...")

    def move_files(files, destination):
        for file_name in files:
            src = os.path.join(SOURCE_DIR, file_name)
            dst = os.path.join(destination, file_name)
            try:
                shutil.move(src, dst)
            except Exception as e:
                print(f" [X] Error al mover {file_name}: {e}")

  
    move_files(val_files, VAL_DIR)
    move_files(test_files, TEST_DIR)


    print("\n--- Distribución Final ---")
    print(f"TRAIN: {len(get_file_list(SOURCE_DIR))} (aprox 80%)")
    print(f"VAL:   {len(get_file_list(VAL_DIR))} (10%)")
    print(f"TEST:  {len(get_file_list(TEST_DIR))} (10%)")