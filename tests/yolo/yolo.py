#!/home/ubuntu/venv/bin/python3
import cv2
import numpy as np
import onnxruntime as ort
import argparse
import os
import sys

# Configuration
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Construct the full path to the model file in that same directory
MODEL_PATH = os.path.join(SCRIPT_DIR, "yolov7-tiny_640x640.onnx")

def letterbox(img, new_shape=(640, 640), color=(114, 114, 114)):
    # You MUST still resize the image, or the model will crash.
    shape = img.shape[:2]
    r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])
    new_unpad = int(round(shape[1] * r)), int(round(shape[0] * r))
    dw, dh = new_shape[1] - new_unpad[0], new_shape[0] - new_unpad[1]
    dw /= 2
    dh /= 2
    if shape[::-1] != new_unpad:
        img = cv2.resize(img, new_unpad, interpolation=cv2.INTER_LINEAR)
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
    img = cv2.copyMakeBorder(img, top, bottom, left, right, cv2.BORDER_CONSTANT, value=color)
    return img

np.set_printoptions(threshold=sys.maxsize)
# 1. Load Model
session = ort.InferenceSession(MODEL_PATH, providers=['CPUExecutionProvider'])
input_name = session.get_inputs()[0].name

# Initialize argument parser
parser = argparse.ArgumentParser(description="Run ONNX inference on a specific image.")

# Add the image path argument (it is required)
parser.add_argument("image_path", type=str, help="Path to the image file to classify")

# Parse the arguments
args = parser.parse_args()

# Check if the file actually exists before crashing the model
if not os.path.exists(args.image_path):
    print(f"Error: The file '{args.image_path}' was not found.")
    sys.exit(1)

# 2. Prepare Image
img0 = cv2.imread(args.image_path)
img = letterbox(img0)                     # Resize to 640x640
img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB) # BGR to RGB
img = img.transpose((2, 0, 1))             # Rearrange channels
img = np.expand_dims(img, 0)               # Add batch dim
img = np.ascontiguousarray(img, dtype=np.float32) / 255.0 # Normalize

# 3. RUN MODEL
outputs = session.run(None, {input_name: img})
raw_data = outputs[0]  # Shape is likely (1, 25200, 85)

# --- SIMPLIFIED FILTERING ---

# 1. Remove the batch dimension to get a 2D matrix (25200 rows, 85 cols)
predictions = raw_data[0]

# 2. Define a confidence threshold (e.g., 50%)
confidence_threshold = 0.1

# 3. Create a filter: look at column 4 (Objectness) for every row
# This creates a list of True/False values
mask = predictions[:, 4] > confidence_threshold

# 4. Apply the filter to keep only the "True" rows
filtered_data = predictions[mask]

print(filtered_data[0] if len(filtered_data) > 0 else "None")
