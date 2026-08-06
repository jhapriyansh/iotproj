from fastapi import FastAPI, Request, Form, Depends, BackgroundTasks
from fastapi.responses import HTMLResponse, RedirectResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from datetime import datetime
from pymongo import MongoClient
from bson import ObjectId
import asyncio
import json
import bcrypt
import jwt
import os

from describe import describe_image
from tts import speak
from dotenv import load_dotenv

load_dotenv()

app = FastAPI()

# ── MongoDB ──
mongo = MongoClient(os.getenv("MONGO_URI", "mongodb://localhost:27017"))
db = mongo["blind_assist"]
users_col = db["users"]
images_col = db["images"]
collisions_col = db["collisions"]

# Ensure unique email index
users_col.create_index("email", unique=True)

# ── Templates & static files ──
templates = Jinja2Templates(directory="templates")
app.mount("/static", StaticFiles(directory="static"), name="static")

SAVE_DIR = "received_images"
os.makedirs(SAVE_DIR, exist_ok=True)
app.mount("/images", StaticFiles(directory=SAVE_DIR), name="images")

SECRET_KEY = os.getenv("SECRET_KEY", "change-me-to-a-random-secret")

# ── SSE event bus ──
sse_clients: list[asyncio.Queue] = []


async def broadcast(event_type: str, data: dict):
    """Push an SSE event to all connected dashboard clients."""
    payload = f"event: {event_type}\ndata: {json.dumps(data, default=str)}\n\n"
    for q in list(sse_clients):
        try:
            q.put_nowait(payload)
        except Exception:
            pass


# ── Auth helpers ──
def hash_password(pw: str) -> str:
    return bcrypt.hashpw(pw.encode(), bcrypt.gensalt()).decode()


def verify_password(pw: str, hashed: str) -> bool:
    return bcrypt.checkpw(pw.encode(), hashed.encode())


def create_token(user_id: str) -> str:
    return jwt.encode({"user_id": user_id}, SECRET_KEY, algorithm="HS256")


def get_current_user(request: Request):
    token = request.cookies.get("token")
    if not token:
        return None
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=["HS256"])
        return users_col.find_one({"_id": ObjectId(payload["user_id"])})
    except Exception:
        return None


# ── Auth routes ──
@app.get("/login", response_class=HTMLResponse)
async def login_page(request: Request):
    return templates.TemplateResponse(request, "login.html", {})


@app.post("/login")
async def login(request: Request, email: str = Form(...), password: str = Form(...)):
    user = users_col.find_one({"email": email})
    if not user or not verify_password(password, user["password"]):
        return templates.TemplateResponse(
            request, "login.html", {"error": "Invalid email or password"}
        )
    token = create_token(str(user["_id"]))
    response = RedirectResponse(url="/dashboard", status_code=303)
    response.set_cookie("token", token, httponly=True, samesite="lax")
    return response


@app.get("/register", response_class=HTMLResponse)
async def register_page(request: Request):
    return templates.TemplateResponse(request, "register.html", {})


@app.post("/register")
async def register(
    request: Request,
    blind_name: str = Form(...),
    guardian_name: str = Form(...),
    email: str = Form(...),
    password: str = Form(...),
):
    if users_col.find_one({"email": email}):
        return templates.TemplateResponse(
            request, "register.html", {"error": "Email already registered"}
        )
    users_col.insert_one(
        {
            "blind_name": blind_name,
            "guardian_name": guardian_name,
            "email": email,
            "password": hash_password(password),
            "created_at": datetime.now(),
        }
    )
    return RedirectResponse(url="/login?registered=1", status_code=303)


@app.get("/logout")
async def logout():
    response = RedirectResponse(url="/login")
    response.delete_cookie("token")
    return response


# ── Dashboard pages ──
@app.get("/", response_class=HTMLResponse)
async def root(request: Request):
    if get_current_user(request):
        return RedirectResponse(url="/dashboard")
    return RedirectResponse(url="/login")


@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard(request: Request):
    user = get_current_user(request)
    if not user:
        return RedirectResponse(url="/login")
    imgs = list(images_col.find().sort("timestamp", -1))
    return templates.TemplateResponse(
        request, "dashboard.html", {"user": user, "images": imgs}
    )


@app.get("/collisions", response_class=HTMLResponse)
async def collisions_page(request: Request):
    user = get_current_user(request)
    if not user:
        return RedirectResponse(url="/login")
    events = list(collisions_col.find().sort("timestamp", -1))
    return templates.TemplateResponse(
        request, "collisions.html", {"user": user, "collisions": events}
    )


# ── SSE stream ──
@app.get("/events")
async def sse_stream(request: Request):
    user = get_current_user(request)
    if not user:
        return RedirectResponse(url="/login")

    queue: asyncio.Queue = asyncio.Queue()
    sse_clients.append(queue)

    async def event_generator():
        try:
            while True:
                if await request.is_disconnected():
                    break
                try:
                    msg = await asyncio.wait_for(queue.get(), timeout=30)
                    yield msg
                except asyncio.TimeoutError:
                    yield ": keepalive\n\n"
        finally:
            sse_clients.remove(queue)

    return StreamingResponse(event_generator(), media_type="text/event-stream",
                             headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


# ── ESP32 endpoints ──
def process_image(path: str, filename: str, timestamp, doc_id):
    """Run AI description + TTS in background so ESP32 gets instant response."""
    try:
        description = describe_image(path)
        print("🧠", description)
        speak(description)
        images_col.update_one({"_id": doc_id}, {"$set": {"description": description}})
        # Push description update to dashboards
        import asyncio as _aio
        loop = _aio.new_event_loop()
        loop.run_until_complete(broadcast("image_described", {
            "filename": filename,
            "description": description,
        }))
        loop.close()
    except Exception as e:
        print("❌ Vision/TTS error:", e)


@app.post("/upload")
async def upload_image(request: Request, bg: BackgroundTasks):
    data = await request.body()

    timestamp = datetime.now()
    filename = f"esp32_{timestamp.strftime('%Y%m%d_%H%M%S_%f')}.jpg"
    path = os.path.join(SAVE_DIR, filename)

    with open(path, "wb") as f:
        f.write(data)

    print(f"📷 Saved: {path}")

    # Save to DB immediately (description filled in later by background task)
    result = images_col.insert_one(
        {
            "filename": filename,
            "description": None,
            "timestamp": timestamp,
        }
    )

    # Notify dashboards immediately (description pending)
    await broadcast("new_image", {
        "filename": filename,
        "description": None,
        "timestamp": timestamp.strftime("%b %d, %Y  %I:%M:%S %p"),
    })

    # AI describe + TTS runs AFTER the response is sent
    bg.add_task(process_image, path, filename, timestamp, result.inserted_id)

    return {"status": "ok", "file": filename}


@app.post("/collision")
async def collision_alert(request: Request):
    try:
        body = await request.json()
        distance = body.get("distance", 0)
    except Exception:
        distance = 0

    now = datetime.now()
    collisions_col.insert_one(
        {
            "timestamp": now,
            "distance": distance,
        }
    )

    await broadcast("new_collision", {
        "distance": distance,
        "timestamp": now.strftime("%b %d, %Y  %I:%M:%S %p"),
    })

    print(f"⚠️  Collision alert! Distance: {distance} cm")
    return {"status": "ok"}
