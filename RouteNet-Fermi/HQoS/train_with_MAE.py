#!/usr/bin/env python3
import json
import os
import numpy as np
import tensorflow as tf
import sys
import glob as glob
import pickle
import random

# Desactivar logs innecesarios de TF
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"

sys.path.append("../")
from data_generatorHQoS import input_fn
from delay_model_HQoSlog import RouteNet_Fermi

# -----------------------------
# 1. REPRODUCIBILIDAD
# -----------------------------
random.seed(42)
np.random.seed(42)
tf.random.set_seed(42)

# -----------------------------
# 2. CONFIGURACIÓN Y RUTAS
# -----------------------------
LEARNING_RATE = 0.0005  # Empezamos un poco más bajo para estabilidad
TRAIN_PATH = "../datasets/oran-hqos-spwfq-sp_ALL/train"
TEST_PATH = "../datasets/oran-hqos-spwfq-sp_ALL/validation"
CKPT_DIR = "./HQoS_checkpoints_MAE_LOG"
ZSCORE_PATH = "../datasets/oran-hqos-spwfq-sp_ALL/zscore_stats.json"
os.makedirs(CKPT_DIR, exist_ok=True)
LOG_CSV = os.path.join(CKPT_DIR, "learning_curve_mae.csv")

EPOCHS = 500
BATCH_SIZE = 8 # Batch pequeño para mayor precisión en GNN

# -----------------------------
# 3. TRANSFORMACIÓN LOGARÍTMICA
# -----------------------------
def log_labels(x, y):
    """
    Transformamos el label a logaritmo de microsegundos (y * 1e6).
    Esto hace que el MAE optimice el error relativo directamente.
    """
    return x, tf.math.log(y * 1e6 + 1e-12)

# -----------------------------
# 4. PREPARACIÓN DEL DATASET
# -----------------------------
print("[*] Cargando datasets...")
ds_train = input_fn(TRAIN_PATH, shuffle=True, seed=42)
ds_train = ds_train.map(log_labels)

ds_eval = input_fn(TEST_PATH, shuffle=False)
ds_eval = ds_eval.map(log_labels)

# Contar muestras para ajustar steps_per_epoch
num_samples = 0
for _ in input_fn(TRAIN_PATH, shuffle=False):
    num_samples += 1

STEPS_PER_EPOCH = num_samples // BATCH_SIZE
ds_train = ds_train.repeat()

print(f" -> Muestras de entrenamiento: {num_samples}")
print(f" -> Steps por época: {STEPS_PER_EPOCH}")

# -----------------------------
# 5. INICIALIZACIÓN DEL MODELO
# -----------------------------
model = RouteNet_Fermi()

# Cargar Z-Score (Normalización de entrada)
zscore_path = ZSCORE_PATH
if os.path.exists(zscore_path):
    with open(zscore_path, 'r') as f:
        loaded_z_score = json.load(f)
    if not hasattr(model, 'z_score') or model.z_score is None:
        model.z_score = {}
    model.z_score.update(loaded_z_score)
    print("[*] Z-Score cargado y aplicado al modelo.")

# Intentar cargar pesos previos
checkpoints = glob.glob(os.path.join(CKPT_DIR, "epoch_*_mae_*.index"))
if checkpoints:
    checkpoints.sort(key=lambda x: float(x.split('mae_')[1].split('.index')[0]))
    best_checkpoint = checkpoints[0].replace('.index', '')
    print(f"[!] Cargando pesos desde: {best_checkpoint}")
    
    # Construcción dummy para inicializar variables
    for x_dummy, _ in ds_eval.take(1):
        _ = model(x_dummy, training=False)
    
    model.load_weights(best_checkpoint).expect_partial()
else:
    print("[!] Iniciando entrenamiento desde cero.")

# -----------------------------
# 6. COMPILACIÓN (MAE + MAPE)
# -----------------------------
optimizer = tf.keras.optimizers.Adam(learning_rate=LEARNING_RATE)
# Usamos MAE como pérdida principal
loss_object = tf.keras.losses.MeanAbsoluteError()

model.compile(
    loss=loss_object,
    optimizer=optimizer,
    metrics=[tf.keras.metrics.MeanAbsolutePercentageError()] # Monitorizamos MAPE
)

# -----------------------------
# 7. CALLBACKS ESTRATÉGICOS
# -----------------------------
callbacks = [
    tf.keras.callbacks.ModelCheckpoint(
        filepath=os.path.join(CKPT_DIR, "epoch_{epoch:02d}_mae_{val_loss:.4f}"),
        save_best_only=True,
        save_weights_only=True,
        monitor='val_loss'
    ),
    tf.keras.callbacks.ReduceLROnPlateau(
        monitor='val_loss',
        factor=0.2,       # Reducción agresiva para afinar el 2%
        patience=12,      # Un poco más de paciencia para GNN
        min_lr=1e-7,
        verbose=1
    ),
    tf.keras.callbacks.EarlyStopping(
        monitor='val_loss',
        patience=40,      # No detenerse demasiado pronto
        restore_best_weights=True,
        verbose=1
    ),
    tf.keras.callbacks.CSVLogger(LOG_CSV, append=True),
    tf.keras.callbacks.TerminateOnNaN()
]

# -----------------------------
# 8. EJECUCIÓN DEL ENTRENAMIENTO
# -----------------------------
print("\n[*] Iniciando entrenamiento...")
history = model.fit(
    ds_train,
    epochs=EPOCHS,
    steps_per_epoch=STEPS_PER_EPOCH,
    validation_data=ds_eval,
    callbacks=callbacks,
    use_multiprocessing=True
)

# Guardar historial final
history_path = os.path.join(CKPT_DIR, 'history_final.pkl')
with open(history_path, 'wb') as f:
    pickle.dump(history.history, f)

print("\n[DONE] Entrenamiento finalizado.")