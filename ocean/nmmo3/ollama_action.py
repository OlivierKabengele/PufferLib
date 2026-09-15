#!/usr/bin/env python3
import json
import os
import sys
import urllib.error
import urllib.request

from action_prompt import build_action_prompt, build_batch_action_prompt, parse_action_code

OLLAMA_URL = os.getenv("NMMO3_OLLAMA_URL", "http://127.0.0.1:11434/api/generate")
MODEL = os.getenv("NMMO3_OLLAMA_MODEL", "qwen3:4b-thinking")
TIMEOUT = float(os.getenv("NMMO3_OLLAMA_TIMEOUT", "180"))


def main():
    context = sys.stdin.read()
    if not context.strip():
        raise SystemExit("empty NMMO3 action context")
    if context.startswith("NMMO3_ACTION_BATCH\n"):
        context = context[len("NMMO3_ACTION_BATCH\n"):]
        blocks = []
        for block in context.split("\nEND_AGENT\n"):
            if not block.startswith("AGENT "):
                continue
            header, agent_context = block.split("\n", 1)
            blocks.append((int(header.split()[1]), agent_context))
        prompt = build_batch_action_prompt(blocks)
        # 64 tokens only covers a single {action_code, confidence} object;
        # a batch response needs one such object per agent plus JSON overhead.
        num_predict = 48 * len(blocks) + 64
        # Each agent context includes a full local map window; a fixed 4096
        # window truncates/drops input once more than ~2-3 agents are batched.
        num_ctx = max(4096, 1536 * len(blocks) + 1024)
    else:
        prompt = build_action_prompt(context)
        num_predict = 64
        num_ctx = 4096
    payload = {
        "model": MODEL,
        "prompt": prompt,
        "stream": False,
        "think": False,
        "format": "json",
        "options": {"temperature": 0.2, "num_ctx": num_ctx, "num_predict": num_predict},
    }
    request = urllib.request.Request(
        OLLAMA_URL, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"}, method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            result = json.load(response)
        action = json.loads(result.get("response", ""))
        if isinstance(action, dict) and "actions" in action:
            for item in action["actions"]:
                item["action_code"] = parse_action_code(item["action_code"])
            print(json.dumps(action))
            return
        if not isinstance(action, dict):
            raise ValueError("action response must be an object")
        code = parse_action_code(action["action_code"])
        print(json.dumps({"action_code": code, "confidence": float(action.get("confidence", 0.0))}))
    except (KeyError, TypeError, ValueError, json.JSONDecodeError, urllib.error.URLError) as error:
        raise SystemExit(str(error))


if __name__ == "__main__":
    main()