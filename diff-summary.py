import os
import requests
from openai import OpenAI

token = os.environ["API_TOKEN"]
client = OpenAI(
    api_key=token, base_url="https://dashscope.aliyuncs.com/compatible-mode/v1"
)
patch = requests.get(
    f"https://patch-diff.githubusercontent.com/raw/dtcxzyw/llvm-opt-benchmark/pull/2103.diff"
).text
# patch = "\n".join(patch.splitlines()[:3000])
max_window = 120_000
patch = patch[: max_window * 2]

completion = client.chat.completions.create(
    model="qwen-plus-latest",
    messages=[
        {
            "role": "system",
            "content": "You are a senior LLVM maintainer. You are reviewing LLVM IR diffs. Please provide a brief summary of the given changes (within 500 words). Please tell me up to 5 major changes for this patch.",
        },
        {
            "role": "user",
            "content": patch,
        },
    ],
    stream=True,
    timeout=600,
    max_completion_tokens=1000,
    stream_options={"include_usage": True},
)

for chunk in completion:
    if len(chunk.choices) == 0:
        print("\n")
        print(chunk.usage)
    else:
        print(chunk.choices[0].delta.content, end="")
