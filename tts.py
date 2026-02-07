import subprocess

def speak(text):
    if text:
        subprocess.run(["say", "-v", "Alex", text])
