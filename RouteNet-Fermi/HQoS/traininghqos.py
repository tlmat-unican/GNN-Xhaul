#!/usr/bin/env python3
import os
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np
import tensorflow as tf
import sys
import glob as glob
import pickle
sys.path.append("../")
from data_generatorHQoS import input_fn
from delay_model import RouteNet_Fermi

# -----------------------------
# RUTAS
# -----------------------------
LEARNING_RATE = 0.001
TRAIN_PATH = "../data/oranallcfgsmixed/train"
TEST_PATH = "../data/oranallcfgsmixed/validation"
CKPT_DIR = "./oran_ckpt32"
os.makedirs(CKPT_DIR, exist_ok=True)
LOG_CSV = os.path.join(CKPT_DIR, "learning_curve.csv")
# -----------------------------
# PARÁMETROS
# -----------------------------
# NUM_SAMPLES_TOTAL = 878
# NUM_SAMPLES_TOTAL = 1373
NUM_SAMPLES_TOTAL = 3802

EPOCHS = 300
BATCH_SIZE = 32
STEPS_PER_EPOCH = NUM_SAMPLES_TOTAL // BATCH_SIZE

# -----------------------------
# FUNCIONES DE NORMALIZACIÓN
# -----------------------------
def compute_normalization_stats(data_dir: str):
    features_to_normalize = [
        'traffic', 'packets', 'eq_lambda', 'avg_pkts_lambda', 'exp_max_factor',
        'pkts_lambda_on', 'avg_t_off', 'avg_t_on', 'ar_a', 'sigma',
        'capacity', 'queue_size'
    ]
    stats = {feat: [] for feat in features_to_normalize}
    ds = input_fn(data_dir, shuffle=False)
    for inputs, _ in ds:
        for feat in features_to_normalize:
            if feat in inputs:
                values = inputs[feat].numpy().flatten()
                values = values[np.isfinite(values) & (values != 0)]
                stats[feat].extend(values.tolist())
    z_score = {feat: [float(np.mean(stats[feat])), float(np.std(stats[feat]))] if stats[feat] else [0.0, 1.0]
               for feat in features_to_normalize}
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
ds_train = input_fn(TRAIN_PATH, shuffle=True, seed=42).repeat()
ds_eval  = input_fn(TEST_PATH, shuffle=False)  # evaluamos sobre el mismo dataset para ver si memorizó

# -----------------------------
# MODELO DESDE CERO
# -----------------------------
model = RouteNet_Fermi()

# Normalización
z_score_stats = compute_normalization_stats(TRAIN_PATH)
# print(z_score_stats)
update_model_z_score(model, z_score_stats)
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
# loss_object = tf.keras.losses.MeanSquaredError()
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
    batch_size=BATCH_SIZE,
    validation_data=ds_eval,
    callbacks=callbacks,
    use_multiprocessing=True
)

history_path = os.path.join(CKPT_DIR, 'history_final.pkl')
with open(history_path, 'wb') as f:
    pickle.dump(history.history, f)


