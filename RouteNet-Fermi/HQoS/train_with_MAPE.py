#!/usr/bin/env python3
import json
import os
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np
import tensorflow as tf
import sys
import glob as glob
import pickle
sys.path.append("../")
from data_generatorHQoS import input_fn
from delay_model_HQoS import RouteNet_Fermi
import random

random.seed(42)
np.random.seed(42)
tf.random.set_seed(42)
# -----------------------------
# RUTAS
# -----------------------------
LEARNING_RATE = 0.001
TRAIN_PATH = "../final_set_dataset/oran-hqos_merged_ALL/train"
TEST_PATH = "../final_set_dataset/oran-hqos_merged_ALL/validation"  # evaluamos sobre el mismo dataset para ver si memorizó
CKPT_DIR = "./CACATEST/"
ZSCORE_PATH = "../final_set_dataset/oran-hqos_merged_ALL/zscore_stats2.json"
os.makedirs(CKPT_DIR, exist_ok=True)
LOG_CSV = os.path.join(CKPT_DIR, "learning_curve02.csv")
# -----------------------------
# PARÁMETROS
# -----------------------------
EPOCHS = 500
STEPS_PER_EPOCH = None
BATCH_SIZE = 8
# -----------------------------
# FUNCIONES DE NORMALIZACIÓN
# -----------------------------
def compute_normalization_stats(data_dirs: list):
    features_to_normalize = [
        'traffic', 'packets', 'eq_lambda', 'avg_pkts_lambda', 'exp_max_factor',
        'pkts_lambda_on', 'avg_t_off', 'avg_t_on', 'ar_a', 'sigma',
        'capacity', 'queue_size'
    ]
    stats = {feat: [] for feat in features_to_normalize}
    
    for data_dir in data_dirs:
        print(f"[*] Analizando datos en: {data_dir}")
        ds = input_fn(data_dir, shuffle=False)
        for inputs, _ in ds:
            for feat in features_to_normalize:
                if feat in inputs:
                    values = inputs[feat].numpy().flatten()
                    stats[feat].extend(values.tolist())
    
    z_score = {}
    for feat in features_to_normalize:
        if stats[feat]:
            mean_val = float(np.mean(stats[feat]))
            std_val = float(np.std(stats[feat]))
            
            # --- MEJORA: Si la desviación es 0, forzamos a 1 ---
            if std_val == 0:
                print(f" [!] Aviso: {feat} tiene desviación 0. Forzando a 1.0.")
                std_val = 1.0
            
            z_score[feat] = [mean_val, std_val]
        else:
            # Caso por defecto si no hay datos para esa feature
            z_score[feat] = [0.0, 1.0]
            
    return z_score

def update_model_z_score(model, z_score_dict):
    if not hasattr(model, 'z_score') or model.z_score is None:
        model.z_score = {}
    updates = {k: v for k, v in z_score_dict.items() if isinstance(v, list) and v[1] != 0}
    model.z_score.update(updates)
    print(f" Z-Score actualizado para {len(updates)} campos.")

# -----------------------------
# DATASET
# -----------------------------
ds_train = input_fn(TRAIN_PATH, shuffle=True, seed=42)
ds_eval = input_fn(TEST_PATH, shuffle=False)

num_samples = 0
for _ in ds_train:
    num_samples += 1
ds_train = ds_train.repeat()  # Repetimos el dataset para entrenamiento continuo

if STEPS_PER_EPOCH is None:
    STEPS_PER_EPOCH = max(1, num_samples // BATCH_SIZE)  # Ajustamos steps por epoch según el número de muestras

print(f" Número total de muestras en train: {num_samples}")
print(f" Steps por epoch ajustados a: {STEPS_PER_EPOCH}")
# -----------------------------
# MODELO DESDE CERO
# -----------------------------
model = RouteNet_Fermi()

zscore_path = ZSCORE_PATH
if zscore_path and os.path.exists(zscore_path):
    with open(zscore_path, 'r') as f:
        loaded_z_score = json.load(f)
    update_model_z_score(model, loaded_z_score)
    print("[*] Z-Score cargado y aplicado al modelo.")
else:
    print("[!] No se encontró Z-Score precomputado. Calculando a partir de los datasets...")
    z_score_stats = compute_normalization_stats([TRAIN_PATH, TEST_PATH])
    update_model_z_score(model, z_score_stats)
    with open(zscore_path, 'w') as f:
        json.dump(z_score_stats, f, indent=4)
    print(f" Z-Score calculado y guardado en: {zscore_path}")



checkpoints = glob.glob(os.path.join(CKPT_DIR, "epoch_*_loss_*.index"))

if checkpoints:
    # Ordenamos por el valor de pérdida en el nombre del archivo (el menor primero)
    # Ejemplo de nombre: epoch_10_loss_8.5000.index
    checkpoints.sort(key=lambda x: float(x.split('loss_')[1].split('.index')[0]))
    best_checkpoint = checkpoints[0].replace('.index', '')
    
    print(f"\n[!] Checkpoint detectado: {best_checkpoint}")
    
    # Construcción dummy: Necesaria para que la GNN cree sus variables internas antes de cargar pesos
    print("Construyendo grafo del modelo...")
    for x_dummy, _ in ds_eval.take(1):
        _ = model(x_dummy, training=False)
    
    model.load_weights(best_checkpoint).expect_partial()
    print(" Pesos cargados exitosamente. Continuando entrenamiento...")
else:
    print("\n[!] No se encontraron checkpoints. Iniciando desde cero.")
# Compilación

optimizer = tf.keras.optimizers.Adam(learning_rate=LEARNING_RATE)
loss_object = tf.keras.losses.MeanAbsolutePercentageError()
model.compile(loss=loss_object, optimizer=optimizer)

# Callbacks
callbacks = [
    tf.keras.callbacks.ModelCheckpoint(
        filepath=os.path.join(CKPT_DIR, "epoch_{epoch:02d}_loss_{val_loss:.4f}"),
        save_best_only=True,
        save_weights_only=True,
        monitor='val_loss'
    ),
    
    tf.keras.callbacks.ReduceLROnPlateau(
        monitor='val_loss',     # métrica a vigilar
        factor=0.5,             # reduce LR a la mitad
        patience=10,             # epochs sin mejora antes de actuar
        min_lr=1e-6,            # LR mínimo
        verbose=1
    ),
    tf.keras.callbacks.EarlyStopping(
        monitor='val_loss',
        patience=20,
        restore_best_weights=True,
        verbose=1
    ),
    tf.keras.callbacks.CSVLogger(LOG_CSV, append=True),
    tf.keras.callbacks.TerminateOnNaN()
]

# -----------------------------
# ENTRENAMIENTO
# -----------------------------
print("\nIniciando entrenamiento desde cero...")
history = model.fit(
    ds_train,
    epochs=EPOCHS,
    steps_per_epoch=STEPS_PER_EPOCH,
    validation_data=ds_eval,
    callbacks=callbacks,
    use_multiprocessing=True
)

history_path = os.path.join(CKPT_DIR, 'history_final.pkl')
with open(history_path, 'wb') as f:
    pickle.dump(history.history, f)


