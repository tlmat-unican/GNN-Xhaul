#!/usr/bin/env python3
import json
import os
import numpy as np
import tensorflow as tf
import sys
import glob as glob
import pickle
import random

# Configuración de logs de TF
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"

# Importaciones locales
sys.path.append("../")
from data_generatorHQoS import input_fn
from delay_model_HQoSExp import RouteNet_Fermi

# Semillas para reproducibilidad
random.seed(42)
np.random.seed(42)
tf.random.set_seed(42)

# -----------------------------
# RUTAS Y PARÁMETROS
# -----------------------------
LEARNING_RATE = 0.001
TRAIN_PATH = "../final_set_dataset/oran-hqos_BW40_SCS30/train"
TEST_PATH = "../final_set_dataset/oran-hqos_BW40_SCS30/validation"  # evaluamos sobre el mismo dataset para ver si memorizó
CKPT_DIR = "./ckpts_final_1_bacth_logs_EarlyStopping32/"
ZSCORE_PATH = "../final_set_dataset/oran-hqos_BW40_SCS30/zscore_stats2.json"

# Archivos de logs
LOG_BATCH_CSV = os.path.join(CKPT_DIR, "loss_por_batch.csv")
LOG_EPOCH_CSV = os.path.join(CKPT_DIR, "loss_media_por_epoch.csv")

EPOCHS = 500
BATCH_SIZE = 32
STEPS_PER_EPOCH = None

os.makedirs(CKPT_DIR, exist_ok=True)

# -----------------------------
# CALLBACK PERSONALIZADO: BATCH & EPOCH LOSS
# -----------------------------
class DetailedLossLogger(tf.keras.callbacks.Callback):
    def __init__(self, batch_file, epoch_file):
        super().__init__()
        self.batch_file = batch_file
        self.epoch_file = epoch_file
        self.batch_losses = []
        
        # Inicializar archivos con cabeceras
        with open(self.batch_file, 'w') as f:
            f.write("epoch,batch,loss_batch\n")
        with open(self.epoch_file, 'w') as f:
            f.write("epoch,loss_media_train,loss_val, lr\n")

    def on_train_batch_end(self, batch, logs=None):
        loss = logs.get('loss')
        self.batch_losses.append(loss)
        current_epoch = self.model.history.epoch[-1] + 1 if hasattr(self.model, 'history') and self.model.history.epoch else 0
        
        # Guardar loss del batch individual
        with open(self.batch_file, 'a') as f:
            f.write(f"{current_epoch},{batch},{loss:.6f}\n")

    def on_epoch_end(self, epoch, logs=None):
        # Calcular media de los batches de esta epoch
        avg_train_loss = np.mean(self.batch_losses)
        val_loss = logs.get('val_loss')
        
        # Guardar en el log de epochs
        with open(self.epoch_file, 'a') as f:
            f.write(f"{epoch},{avg_train_loss:.6f},{val_loss:.6f},{logs.get('lr', 'N/A')}\n")
        
        # Limpiar lista para la siguiente epoch
        self.batch_losses = []

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
        ds = input_fn(data_dir, shuffle=False)
        for inputs, _ in ds:
            for feat in features_to_normalize:
                if feat in inputs:
                    stats[feat].extend(inputs[feat].numpy().flatten().tolist())
    
    z_score = {}
    for feat in features_to_normalize:
        if stats[feat]:
            m, s = np.mean(stats[feat]), np.std(stats[feat])
            z_score[feat] = [float(m), float(s) if s != 0 else 1.0]
    return z_score

def update_model_z_score(model, z_score_dict):
    if not hasattr(model, 'z_score') or model.z_score is None:
        model.z_score = {}
    model.z_score.update(z_score_dict)

# -----------------------------
# DATASET Y MODELO
# -----------------------------
ds_train = input_fn(TRAIN_PATH, shuffle=True, seed=42)
ds_eval = input_fn(TEST_PATH, shuffle=False)

num_samples = sum(1 for _ in ds_train)
ds_train = ds_train.repeat()
STEPS_PER_EPOCH = max(1, num_samples // BATCH_SIZE)

model = RouteNet_Fermi()

if os.path.exists(ZSCORE_PATH):
    with open(ZSCORE_PATH, 'r') as f:
        update_model_z_score(model, json.load(f))
else:
    z_stats = compute_normalization_stats([TRAIN_PATH, TEST_PATH])
    update_model_z_score(model, z_stats)
    with open(ZSCORE_PATH, 'w') as f:
        json.dump(z_stats, f, indent=4)

# Reanudar si hay checkpoint
checkpoints = glob.glob(os.path.join(CKPT_DIR, "epoch_*_loss_*.index"))
if checkpoints:
    checkpoints.sort(key=lambda x: float(x.split('loss_')[1].split('.index')[0]))
    best_ckpt = checkpoints[0].replace('.index', '')
    for x_dummy, _ in ds_eval.take(1): _ = model(x_dummy, training=False)
    model.load_weights(best_ckpt).expect_partial()
    print(f"[*] Pesos cargados de: {best_ckpt}")

# -----------------------------
# COMPILACIÓN Y ENTRENAMIENTO
# -----------------------------
optimizer = tf.keras.optimizers.Adam(learning_rate=LEARNING_RATE)
model.compile(loss=tf.keras.losses.MeanAbsolutePercentageError(), optimizer=optimizer)

# Lista de Callbacks
callbacks = [
    tf.keras.callbacks.ModelCheckpoint(
        filepath=os.path.join(CKPT_DIR, "epoch_{epoch:02d}_loss_{val_loss:.4f}"),
        save_best_only=True, save_weights_only=True, monitor='val_loss'
    ),
    tf.keras.callbacks.ReduceLROnPlateau(monitor='val_loss', factor=0.5, patience=10, verbose=1),
    tf.keras.callbacks.EarlyStopping(
        monitor='val_loss',
        patience=20,
        restore_best_weights=True,
        verbose=1
    ),
    DetailedLossLogger(LOG_BATCH_CSV, LOG_EPOCH_CSV), 
    tf.keras.callbacks.TerminateOnNaN()
]

print(f"\nIniciando entrenamiento. Info en: {CKPT_DIR}")
history = model.fit(
    ds_train,
    epochs=EPOCHS,
    steps_per_epoch=STEPS_PER_EPOCH,
    validation_data=ds_eval,
    callbacks=callbacks
)

# Guardar historial final
with open(os.path.join(CKPT_DIR, 'history_final.pkl'), 'wb') as f:
    pickle.dump(history.history, f)