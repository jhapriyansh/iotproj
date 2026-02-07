from groq import Groq
import base64
import os

client = Groq(api_key=os.environ["GROQ_API_KEY"])

PROMPT = """
You are assisting a blind person with navigation.

Describe only what directly affects safe movement in front of the camera.
Focus on:
- obstacles or hazards
- people or moving objects
- doors or entrances, and always state their position if visible
- whether the path ahead is clear or blocked

Use short sentences.
Use clear directions like left, right, center, near, far.
Do not guess or assume.
Do not describe colors.

Limit the response to 2–3 short sentences.
"""


def encode_image(path):
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode()

def describe_image(path):
    img = encode_image(path)

    res = client.chat.completions.create(
        model="meta-llama/llama-4-scout-17b-16e-instruct",
        messages=[{
            "role": "user",
            "content": [
                {"type": "text", "text": PROMPT},
                {
                    "type": "image_url",
                    "image_url": {
                        "url": f"data:image/jpeg;base64,{img}"
                    }
                }
            ]
        }],
        temperature=0.2,
        max_completion_tokens=100,  # hard cap to prevent rambling
    )

    return res.choices[0].message.content.strip()
