#!/usr/bin/env python3
"""Turn an NMMO3 strategy context into one validated action decision using OpenAI GPT."""

import json
import os
import sys
import urllib.error
import urllib.request

from strategy_prompt import build_strategy_prompt

MODEL = os.getenv("NMMO3_GPT_MODEL", "gpt-5.4-nano")
REQUEST_TIMEOUT = float(os.getenv("NMMO3_GPT_TIMEOUT", "30"))


def get_api_key():
    key = os.getenv("OPENAI_API_KEY")
    if key and key.strip() and not key.startswith("your_") and not key.startswith("sk-YourActual"):
        return key.strip()
    # Check .env file in workspace root or current directory
    candidates = [
        os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), ".env"),
        os.path.join(os.getcwd(), ".env"),
        os.path.expanduser("~/.env"),
    ]
    for env_file in candidates:
        if os.path.isfile(env_file):
            try:
                with open(env_file, "r") as f:
                    for line in f:
                        line = line.strip()
                        if line.startswith("OPENAI_API_KEY="):
                            val = line.split("=", 1)[1].strip().strip("'\"")
                            if val and not val.startswith("your_") and not val.startswith("sk-YourActual"):
                                return val
            except Exception:
                pass
    return None


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


def query_gpt(strategy_context):
    api_key = get_api_key()
    if not api_key:
        raise ValueError("OPENAI_API_KEY environment variable (or .env file) is missing.")

    url = "https://api.openai.com/v1/chat/completions"
    prompt = build_strategy_prompt(strategy_context)

    payload = {
        "model": MODEL,
        "messages": [
            {"role": "user", "content": prompt}
        ],
        "response_format": {"type": "json_object"},
        "temperature": 0.2
    }

    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}"
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT) as response:
            data = json.load(response)
            usage = data["usage"]
            print(f"Input tokens: {usage['prompt_tokens']}", file=sys.stderr)
            print(f"Output tokens: {usage['completion_tokens']}", file=sys.stderr)
            print(f"Total tokens: {usage['total_tokens']}", file=sys.stderr)
            text = data["choices"][0]["message"]["content"]
            return parse_strategy_response(text)
    except urllib.error.HTTPError as http_err:
        err_body = http_err.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP Error {http_err.code}: {http_err.reason} - {err_body}") from http_err


def main():
    context = sys.stdin.read()
    if not context.strip():
        raise SystemExit("Empty NMMO3 strategy context")
    try:
        result = query_gpt(context)
        print(json.dumps(result))
    except Exception as error:
        print(f"GPT request failed: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit(130)
