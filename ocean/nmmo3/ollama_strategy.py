#!/usr/bin/env python3
"""Turn an NMMO3 strategy context into one validated action decision."""

import json
import sys
import fcntl
import http.client
import os
import urllib.request
import urllib.error

from strategy_prompt import build_strategy_prompt

OLLAMA_URL = os.getenv("NMMO3_QWEN3_URL", "http://127.0.0.1:11434/api/generate")
MODEL = os.getenv("NMMO3_QWEN3_MODEL", "qwen3:4b-thinking")
REQUEST_TIMEOUT = float(os.getenv("NMMO3_QWEN3_TIMEOUT", "180"))
CONNECTION_RETRIES = 3
ACTION_MAP = {
    "MOVE_DOWN": 0,
    "MOVE_UP": 1,
    "MOVE_RIGHT": 2,
    "MOVE_LEFT": 3,
    "NOOP": 4,
    "ATTACK": 5,
    "USE_ITEM_1": 8,
    "USE_ITEM_2": 9,
    "USE_ITEM_3": 10,
    "USE_ITEM_4": 11,
    "USE_ITEM_5": 12,
    "USE_ITEM_6": 13,
    "USE_ITEM_7": 14,
    "USE_ITEM_8": 15,
    "USE_ITEM_9": 16,
    "USE_ITEM_0": 17,
    "USE_ITEM_MINUS": 18,
    "USE_ITEM_EQUALS": 19,
    "BUY": 20,
    "SELL": 21,
    "MOVE_DOWN_SHIFT": 22,
    "MOVE_UP_SHIFT": 23,
    "MOVE_RIGHT_SHIFT": 24,
    "MOVE_LEFT_SHIFT": 25,
}


def parse_strategy_response(response_text):
    response_text = response_text.strip()
    if response_text.startswith("```"):
        response_text = response_text.split("\n", 1)[-1]
        response_text = response_text.rsplit("```", 1)[0].strip()
    permitted_strategies = {
        "explore", "harvest", "equip", "trade", "engage_NPC",
        "avoid_combat", "retreat", "recover",
    }
    strategy = json.loads(response_text)
    if not isinstance(strategy, dict):
        raise ValueError("strategy response must be a JSON object")
    if strategy.get("strategy_id") not in permitted_strategies:
        raise ValueError("invalid strategy_id")
    required_sections = (
        "strategy_parameters", "risk_parameters", "social_parameters",
        "contingency_parameters",
    )
    if any(section not in strategy or not isinstance(strategy[section], dict)
           for section in required_sections):
        raise ValueError("strategy response is missing a required section")
    if "confidence" not in strategy:
        raise ValueError("strategy response is missing confidence")
    return strategy


def query_ollama(strategy_context):
    prompt = build_strategy_prompt(strategy_context)
    request = urllib.request.Request(
        OLLAMA_URL,
        data=json.dumps({
            "model": MODEL,
            "prompt": prompt,
            "stream": False,
            "think": False,
            "temperature": 0.2,
            "keep_alive": "10m",
            "format": "json",
            "options": {
                "num_ctx": 2048,
                "num_predict": 256,
                "temperature": 0.2,
            },
        }).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with open("/tmp/nmmo3-qwen3.lock", "w") as lock_file:
        fcntl.flock(lock_file, fcntl.LOCK_EX)
        for attempt in range(CONNECTION_RETRIES):
            try:
                with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT) as response:
                    payload = json.load(response)
                    input_tokens = payload["prompt_eval_count"]
                    output_tokens = payload["eval_count"]
                    print(f"Input tokens: {input_tokens}", file=sys.stderr)
                    print(f"Output tokens: {output_tokens}", file=sys.stderr)
                    print(f"Total tokens: {input_tokens + output_tokens}", file=sys.stderr)
                break
            except urllib.error.URLError as error:
                refused = isinstance(error.reason, ConnectionRefusedError)
                if not refused or attempt == CONNECTION_RETRIES - 1:
                    raise
                import time
                time.sleep(2 ** attempt)
        else:
            raise urllib.error.URLError("Ollama connection retry limit reached")
    return parse_strategy_response(payload.get("response", ""))


def main():
    context = sys.stdin.read()
    if not context.strip():
        raise SystemExit("empty NMMO3 strategy context")
    try:
        result = query_ollama(context)
    except (
        urllib.error.URLError,
        http.client.RemoteDisconnected,
        TimeoutError,
        OSError,
        ValueError,
        json.JSONDecodeError,
    ) as error:
        raise SystemExit(1)
    print(json.dumps(result))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit(130)
