#!/usr/bin/env python3
import json
import os
import sys
import urllib.error
import urllib.request

from action_prompt import build_action_prompt, build_batch_action_prompt, parse_action_code

MODEL = os.getenv("NMMO3_OPENROUTER_MODEL", "google/gemma-4-31b-it:free")
TIMEOUT = float(os.getenv("NMMO3_OPENROUTER_TIMEOUT", "60"))
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"


def main():
    context = sys.stdin.read()
    if not context.strip():
        raise SystemExit("empty NMMO3 action context")
    key = os.getenv("OPENROUTER_API_KEY")
    if not key:
        raise SystemExit("OPENROUTER_API_KEY is missing")
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
    request = urllib.request.Request(
        OPENROUTER_URL,
        data=json.dumps(payload).encode(),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {key}",
            "HTTP-Referer": "https://github.com/3046857-git-EDUC/PufferLib",
            "X-Title": "PufferLib NMMO3 Direct Action",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            result = json.load(response)
        action = json.loads(result["choices"][0]["message"]["content"])
        if not isinstance(action, dict):
            raise ValueError("action response must be an object")
        if "actions" in action:
            for item in action["actions"]:
                item["action_code"] = parse_action_code(item["action_code"])
            print(json.dumps(action))
            return
        code = parse_action_code(action["action_code"])
        print(json.dumps({
            "action_code": code,
            "confidence": float(action.get("confidence", 0.0)),
        }))
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")
        raise SystemExit(f"OpenRouter HTTP Error {error.code}: {body}") from error
    except (KeyError, TypeError, ValueError, json.JSONDecodeError,
            urllib.error.URLError) as error:
        raise SystemExit(str(error))


if __name__ == "__main__":
    main()