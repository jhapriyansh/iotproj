import subprocess

def speak(text: str):
    if not text:
        return

    subprocess.run([
        "say",
        "-v", "Samantha",   # change voice if you want
        text
    ])
