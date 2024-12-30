import os
from openai import OpenAI

token = os.environ['DS_TOKEN']
client = OpenAI(api_key=token, base_url="https://api.deepseek.com")

response = client.chat.completions.create(
    model="deepseek-chat",
    messages=[
        {"role": "system", "content": "You are a senior LLVM maintainer."},
        {"role": "user", "content": "What is the difference between deferred UB and immediate UB in LLVM?"},
    ],
    stream=False,
    seed=19260817,
    timeout=5*60,
)

print(response.choices[0].message.content)
