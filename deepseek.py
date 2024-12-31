import os
import requests
from openai import OpenAI

token = os.environ['DS_TOKEN']
client = OpenAI(api_key=token, base_url="https://api.deepseek.com")
# pr_id = 120905
# patch = requests.get(f"https://github.com/llvm/llvm-project/pull/{pr_id}.diff").text

response = client.chat.completions.create(
    model="deepseek-chat",
    messages=[
        {"role": "system", "content": "You are a senior LLVM maintainer."},
        # {"role": "user", "content": "What is the difference between deferred UB and immediate UB in LLVM?"},
        # {"role": "user", "content": "What is the meaning of `samesign` flag in LLVM?"},
        # {"role": "user", "content": "What is the meaning of `disjoint` flag in LLVM?"},
        {"role": "user", "content": "Please list all the poison generating flags in LLVM."},
        # {"role": "user", "content": f"Please make three short, concrete and targeted suggestions on the following patch. Don't output in Markdown format. Patch:\n{patch}"},
    ],
    stream=False,
    seed=19260817,
    timeout=60,
)

print(response.choices[0].message.content)
