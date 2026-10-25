#!/usr/bin/env python3
#"""Turn an NMMO3 strategy context into one validated action decision using Google Gemini."""

import json
import os
import sys
import urllib.error
import urllib.request

from strategy_prompt import build_strategy_prompt

GEMINI_API_KEY = os.getenv("GEMINI_API_KEY")
MODEL = os.getenv("NMMO3_GEMINI_MODEL", "gemini-3.5-flash-lite")
REQUEST_TIMEOUT = float(os.getenv("NMMO3_GEMINI_TIMEOUT", "60"))


def get_api_key():
    key = os.getenv("GEMINI_API_KEY")
    if key and key.strip() and not key.startswith("your_") and not key.startswith("AIzaSyYourActual"):
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
                        if line.startswith("GEMINI_API_KEY="):
                            val = line.split("=", 1)[1].strip().strip("'\"")
                            if val and not val.startswith("your_") and not val.startswith("AIzaSyYourActual"):
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


def query_gemini(strategy_context):
    api_key = get_api_key()
    if not api_key:
        raise ValueError("GEMINI_API_KEY environment variable is missing or invalid.")

    url = f"https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent?key={api_key}"
    prompt = build_strategy_prompt(strategy_context)

    payload = {
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {
            "response_mime_type": "application/json",
            "temperature": 0.2,
        },
    }

    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT) as response:
            data = json.load(response)
            text = data["candidates"][0]["content"]["parts"][0]["text"]
            return parse_strategy_response(text)
    except urllib.error.HTTPError as http_err:
        err_body = http_err.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP Error {http_err.code}: {http_err.reason} - {err_body}") from http_err


def main():
    context = sys.stdin.read()
    if not context.strip():
        raise SystemExit("Empty NMMO3 strategy context")
    try:
        result = query_gemini(context)
        print(json.dumps(result))
    except Exception as error:
        print(f"Gemini request failed: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit(130)
