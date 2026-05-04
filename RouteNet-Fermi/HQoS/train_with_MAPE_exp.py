#!/usr/bin/env python3
import json
import os
import numpy as np
import tensorflow as tf
import sys
import pickle
import random
from pathlib import Path

# Configuraciones de sistema
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
sys.path.append("../")
from data_generatorHQoS import input_fn
from delay_model_HQoS import RouteNet_Fermi

# Semillas para reproducibilidad
random.seed(42)
np.random.seed(42)
tf.random.set_seed(42)

# -----------------------------
# CONFIGURACIÓN Y RUTAS
# -----------------------------
LEARNING_RATE = 0.0005  # Bajamos un poco para mayor estabilidad en log
TRAIN_PATH = "../final_set_dataset/oran-hqos_merged_ALL/train"
TEST_PATH = "../final_set_dataset/oran-hqos_merged_ALL/validation"
CKPT_DIR = "./HQoS_checkpoints_log_scale/"
ZSCORE_PATH = "../final_set_dataset/oran-hqos_merged_ALL/zscore_stats2.json"

os.makedirs(CKPT_DIR, exist_ok=True)
LOG_CSV = os.path.join(CKPT_DIR, "learning_curve_log.csv")

EPOCHS = 500
BATCH_SIZE = 16  # Aumentado para gradientes más suaves

# -----------------------------
# FUNCIONES DE ESCALA (CORREGIDAS)
# -----------------------------
def log_transform(x, y):
    """
    Escala a microsegundos (us) y aplica log1p. 
    Esto evita logaritmos negativos gigantes y mejora la precisión.
    """
    y_us = y * 1e6
    return x, tf.math.log1p(y_us)

def mape_lineal(y_true_log, y_pred_log):
    """
    Deshace el log1p usando expm1 para calcular el MAPE en escala lineal (us).
    """
    y_true = tf.math.expm1(y_true_log)
    y_pred = tf.math.expm1(y_pred_log)
    
    abs_err = tf.math.abs(y_true - y_pred)
    # Epsilon de 1e-3 (1 nanosegundo en escala de microsegundos)
    return tf.reduce_mean(abs_err / (y_true + 1e-3)) * 100.0

# -----------------------------
# PREPARACIÓN DE DATOS
# -----------------------------
print("[*] Cargando y transformando datasets...")
ds_train = input_fn(TRAIN_PATH, shuffle=True, seed=42).map(log_transform)
ds_eval = input_fn(TEST_PATH, shuffle=False).map(log_transform)

# Calcular steps (esto puede tardar la primera vez)
print("[*] Calculando steps por epoch...")
num_samples = 0
for _ in ds_train: num_samples += 1
ds_train = ds_train.repeat()
steps_per_epoch = max(1, num_samples // BATCH_SIZE)

# -----------------------------
# MODELO E INICIALIZACIÓN
# -----------------------------
model = RouteNet_Fermi()

if os.path.exists(ZSCORE_PATH):
    with open(ZSCORE_PATH, 'r') as f:
        z_stats = json.load(f)
    if not hasattr(model, 'z_score') or model.z_score is None:
        model.z_score = {}
    # Filtramos varianzas nulas para evitar divisiones por cero
    model.z_score.update({k: v for k, v in z_stats.items() if v[1] > 1e-9})
    print("[*] Z-Score aplicado.")

# -----------------------------
# COMPILACIÓN
# -----------------------------
optimizer = tf.keras.optimizers.Adam(learning_rate=LEARNING_RATE)
model.compile(
    optimizer=optimizer,
    loss='mse', 
    metrics=[mape_lineal]
)

# -----------------------------
# CALLBACKS (CORREGIDOS)
# -----------------------------
# CRÍTICO: Monitoreamos 'val_loss' (MSE en log), no el MAPE.
# El MAPE lineal explota al principio y arruina los callbacks.
callbacks = [
    tf.keras.callbacks.ModelCheckpoint(
        filepath=os.path.join(CKPT_DIR, "best_model_loss_{val_loss:.4f}"),
        save_best_only=True,
        save_weights_only=True,
        monitor='val_loss' 
    ),
    tf.keras.callbacks.ReduceLROnPlateau(
        monitor='val_loss',
        factor=0.5,
        patience=10,
        min_lr=1e-7,
        verbose=1
    ),
    tf.keras.callbacks.EarlyStopping(
        monitor='val_loss',
        patience=30,
        restore_best_weights=True
    ),
    tf.keras.callbacks.CSVLogger(LOG_CSV, append=True)
]

# -----------------------------
# ENTRENAMIENTO
# -----------------------------
print(f"\nIniciando entrenamiento en escala log1p (us).")
history = model.fit(
    ds_train,
    epochs=EPOCHS,
    steps_per_epoch=steps_per_epoch,
    validation_data=ds_eval,
    callbacks=callbacks
)

with open(os.path.join(CKPT_DIR, 'history_log_final.pkl'), 'wb') as f:
    pickle.dump(history.history, f)

print(f"\n[OK] Checkpoints en: {CKPT_DIR}")