from fastapi import FastAPI, Request
from datetime import datetime
import os

from describe import describe_image
from tts import speak

app = FastAPI()

SAVE_DIR = "received_images"
os.makedirs(SAVE_DIR, exist_ok=True)

@app.post("/upload")
async def upload_image(request: Request):
    data = await request.body()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    filename = f"esp32_{timestamp}.jpg"
    path = os.path.join(SAVE_DIR, filename)

    with open(path, "wb") as f:
        f.write(data)

    print(f"📷 Saved: {path}")

    try:
        description = describe_image(path)
        print("🧠", description)
        speak(description)
    except Exception as e:
        print("❌ Vision/TTS error:", e)
        description = None

    return {"status": "ok", "file": filename}
