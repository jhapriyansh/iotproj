from groq import Groq
import base64
import os
from dotenv import load_dotenv

load_dotenv()

client = Groq(api_key=os.environ["GROQ_API_KEY"])

PROMPT = """
You are a navigation assistant for a blind person.

Return ONLY the final navigation description.

Never output reasoning.
Never output chain of thought.
Never output analysis.
Never output <think> tags.
Never mention the user, the camera, or the image.

Describe only information that affects safe navigation:
- Obstacles or hazards
- People or moving objects
- Doors or entrances
- Stairs, ramps, curbs, or steps
- Whether the path ahead is clear or blocked

Use directions such as left, center, right, near, and far.

Only describe what is clearly visible.
Do not guess.
Do not infer anything that is not visible.
Do not mention colors unless they are required for safety.

Keep the response between 2 and 5 short sentences.

Output ONLY the navigation description.
"""


def encode_image(path):
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode("utf-8")


def describe_image(path):
    img = encode_image(path)

    response = client.chat.completions.create(
        model="qwen/qwen3.6-27b",
        messages=[
            {
                "role": "system",
                "content": PROMPT,
            },
            {
                "role": "user",
                "content": [
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": f"data:image/jpeg;base64,{img}"
                        },
                    }
                ],
            },
        ],
        temperature=0.2,
        max_completion_tokens=250,
        reasoning_effort="none",
        reasoning_format="hidden",
    )

    return response.choices[0].message.content.strip()