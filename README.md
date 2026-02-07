# IoT Vision Assistant

This project is an IoT system designed to assist visually impaired individuals by capturing images with an ESP32-CAM camera module and providing real-time audio descriptions of the surroundings for navigation and safety.

## Components

### ESP32-CAM Sketch (`CAM_PUSH/` folder)

The Arduino sketch for the ESP32-CAM module that captures images and transmits them to the server.

**Files:**

- `CAM_PUSH.ino`: Main Arduino sketch handling camera initialization, image capture, and HTTP transmission to the server.
- `camera_pins.h`: Header file defining pin configurations for the OV2640 camera module.

**Setup:**

1. Open `CAM_PUSH.ino` in Arduino IDE.
2. Select ESP32-CAM board from Tools menu.
3. Configure the appropriate COM port.
4. Upload the sketch to your ESP32-CAM.

### Python Server

A FastAPI-based server that receives images from the ESP32-CAM, processes them using AI for scene description, and provides text-to-speech output.

**Files:**

- `server.py`: FastAPI application with `/upload` endpoint for receiving images, processing descriptions, and triggering TTS.
- `describe.py`: Uses Groq API with Llama model to generate navigation-focused image descriptions.
- `tts.py`: Text-to-speech functionality using macOS `say` command.

**Features:**

- Receives JPEG images via HTTP POST.
- Generates concise, practical descriptions focused on obstacles, pathways, and hazards.
- Provides audio feedback through TTS.
- Saves received images to `received_images/` directory.

## Prerequisites

- ESP32-CAM module with OV2640 camera.
- Arduino IDE with ESP32 board support.
- Python 3.8+ with required packages:
  - `fastapi`
  - `uvicorn`
  - `groq`

## Installation & Usage

1. **Hardware Setup:**
   - Connect ESP32-CAM to your network (configure WiFi in the sketch).
   - Ensure camera is properly wired according to `camera_pins.h`.

2. **Server Setup:**

   ```bash
   # Create and activate virtual environment
   python -m venv venv
   source venv/bin/activate  # On macOS/Linux; use venv\Scripts\activate on Windows

   # Install required packages
   pip install -r requirements.txt

   # Set environment variable for Groq API key
   export GROQ_API_KEY=your_api_key_here

   # Run the server
   python server.py
   ```

3. **ESP32 Upload:**
   - Modify the sketch with your server IP and endpoint.
   - Upload to ESP32-CAM.

4. **Run:**
   - Start the server.
   - Power on ESP32-CAM to begin image capture and transmission.

The system will capture image at button press, send them to the server for AI-powered description, and provide audio guidance to assist with navigation.
