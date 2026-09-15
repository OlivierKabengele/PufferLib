#!/usr/bin/env python3
import json
import os
import sys
import urllib.error
import urllib.request

from action_prompt import build_action_prompt, build_batch_action_prompt, parse_action_code

MODEL = os.getenv("NMMO3_GPT_MODEL", "gpt-4o-mini")
TIMEOUT = float(os.getenv("NMMO3_GPT_TIMEOUT", "30"))


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
    else:
        prompt = build_action_prompt(context)
    payload = {
        "model": MODEL,
        "messages": [{"role": "user", "content": prompt}],
        "response_format": {"type": "json_object"},
        "temperature": 0.2,
    }
    key = os.getenv("OPENAI_API_KEY")
    if not key:
        raise SystemExit("OPENAI_API_KEY is missing")
    request = urllib.request.Request(
        "https://api.openai.com/v1/chat/completions",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {key}"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            result = json.load(response)
        action = json.loads(result["choices"][0]["message"]["content"])
        if "actions" in action:
            for item in action["actions"]:
                item["action_code"] = parse_action_code(item["action_code"])
            print(json.dumps(action))
            return
        code = parse_action_code(action["action_code"])
        print(json.dumps({"action_code": code, "confidence": float(action.get("confidence", 0.0))}))
    except (KeyError, TypeError, ValueError, json.JSONDecodeError, urllib.error.URLError) as error:
        raise SystemExit(str(error))


if __name__ == "__main__":
    main()