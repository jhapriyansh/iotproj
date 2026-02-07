from groq import Groq
import base64
import os

client = Groq(api_key=os.environ["GROQ_API_KEY"])

SAFETY_PROMPT = """
You are assisting a blind person.

Describe the scene clearly and calmly.
Focus on:
- obstacles or hazards
- people and their positions
- clear pathways
- objects that may block movement

Use relative positions like left, right, center, near, far.
Do not guess. Do not speculate.
If something is unclear, say it is unclear.
Keep it concise and practical.
"""

def encode_image(image_path: str) -> str:
    with open(image_path, "rb") as f:
        return base64.b64encode(f.read()).decode("utf-8")

def describe_image(image_path: str) -> str:
    base64_image = encode_image(image_path)

    completion = client.chat.completions.create(
        model="meta-llama/llama-4-scout-17b-16e-instruct",
        messages=[
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": SAFETY_PROMPT},
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": f"data:image/jpeg;base64,{base64_image}"
                        }
                    }
                ]
            }
        ],
        temperature=0.2,
        max_completion_tokens=400,
    )

    return completion.choices[0].message.content
