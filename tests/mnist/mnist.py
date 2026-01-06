#!/home/andrea/Desktop/pythonvenv/venv/bin/python3
import onnxruntime as ort
import numpy as np
from PIL import Image
import argparse
import os
import sys

# --- CONFIGURATION ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Construct the full path to the model file in that same directory
MODEL_PATH = os.path.join(SCRIPT_DIR, "mnist-8.onnx")

def preprocess_mnist(image_path):
    """
    Reads an image, converts to grayscale, resizes to 28x28, 
    normalizes, and reshapes for the model.
    """
    # 1. Open image and convert to Grayscale ('L')
    img = Image.open(image_path).convert('L')
    
    # 3. Convert to numpy array
    img_data = np.array(img).astype('float32')
    
    # 4. Normalize (0 to 1 range)
    img_data = img_data / 255.0
    
    # 5. Reshape to (Batch, Channel, Height, Width) -> (1, 1, 28, 28)
    img_data = img_data.reshape(1, 1, 28, 28)
    
    return img_data

def main():
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

    # Load the model
    session = ort.InferenceSession(MODEL_PATH)
    
    # Get the name of the input node dynamically
    input_name = session.get_inputs()[0].name
    
    # Prepare input using the path from arguments
    print(f"Processing image: {args.image_path}")
    input_data = preprocess_mnist(args.image_path)
    
    # Run inference
    result = session.run(None, {input_name: input_data})
    
    
    print(f"{result}")

if __name__ == "__main__":
    main()