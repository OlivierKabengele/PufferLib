"""Ollama-powered Neural MMO 3 strategic planner.

Run:     python ollama-strategic.py context.json --output decision.json
         cat context.json | python ollama-strategic.py -
         python ollama-strategic.py context.json --dry-run

Requires a local Ollama server and strategic_prompt.py alongside this script.
Set OLLAMA_HOST to override the default http://localhost:11434 endpoint.
Writes only validated decision JSON to stdout (or --output); logs and errors
go to stderr.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from urllib import error, request

from strategic_prompt import build_strategic_prompt, parse_strategic_decision

DEFAULT_MODEL = "qwen3:4b-thinking"
DEFAULT_HOST = "http://localhost:11434"


def _ollama_chat(prompt: str, *, model: str, host: str, timeout: float,
                 client=None, usage_callback=None) -> str:
    """Call Ollama's local chat API and return the assistant's response text."""
    if not model:
        raise ValueError("Ollama model must not be empty")
    endpoint = host.rstrip("/") + "/api/chat"
    payload = {
        "model": model,
        "messages": [{"role": "user", "content": prompt}],
        "format": "json",
        "stream": False,
        "think": False,
        "options": {"temperature": 0.2},
    }
    req = request.Request(
        endpoint,
        data=json.dumps(payload).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    transport = client or request.urlopen
    try:
        with transport(req, timeout=timeout) as response:
            data = json.loads(response.read().decode("utf-8"))
    except error.HTTPError as exc:
        detail = exc.read(4096).decode("utf-8", errors="replace")
        raise RuntimeError(f"Ollama API HTTP {exc.code}: {detail}") from exc
    except error.URLError as exc:
        raise RuntimeError(f"Ollama connection error: {exc.reason}") from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError("Ollama returned a non-JSON API response") from exc

    input_tokens = int(data.get("prompt_eval_count", 0) or 0)
    output_tokens = int(data.get("eval_count", 0) or 0)
    if usage_callback is not None:
        usage_callback(input_tokens, output_tokens)
    print(
        f"[strategy_tokens] Input_tokens={input_tokens} "
        f"Output_tokens={output_tokens} model={model}",
        file=sys.stderr,
    )

    message = data.get("message")
    text = message.get("content") if isinstance(message, dict) else None
    if not text:
        raise RuntimeError("Ollama returned no assistant content")
    return text


def generate_decision(context: dict, *, model: str | None = None,
                      host: str | None = None, retries: int = 2,
                      timeout: float = 120.0, client=None,
                      usage_callback=None) -> dict:
    """Return one contract-validated strategic decision from Ollama."""
    if retries < 0 or timeout <= 0:
        raise ValueError("Invalid retries or timeout")
    model = model or os.getenv("OLLAMA_STRATEGIC_MODEL", DEFAULT_MODEL)
    host = host or os.getenv("OLLAMA_HOST", DEFAULT_HOST)
    prompt = build_strategic_prompt(context)
    last_error = None
    for attempt in range(retries + 1):
        contents = prompt
        if last_error is not None:
            contents += (
                "\n\nA previous response failed validation: " + last_error
                + "\nReturn a corrected decision using only observed targets."
            )
        raw = _ollama_chat(
            contents, model=model, host=host, timeout=timeout,
            client=client, usage_callback=usage_callback,
        )
        try:
            return parse_strategic_decision(raw, context)
        except (ValueError, TypeError) as exc:
            last_error = str(exc)
            print(
                f"Validation failed (attempt {attempt + 1}/{retries + 1}): "
                f"{last_error}",
                file=sys.stderr,
            )
    raise RuntimeError(
        f"No valid strategic decision after {retries + 1} attempts: {last_error}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("context", help="Context JSON path or '-' for stdin")
    parser.add_argument("--output", "-o", help="Decision JSON file; defaults to stdout")
    parser.add_argument(
        "--model", default=os.getenv("OLLAMA_STRATEGIC_MODEL", DEFAULT_MODEL)
    )
    parser.add_argument("--host", default=os.getenv("OLLAMA_HOST", DEFAULT_HOST))
    parser.add_argument("--retries", type=int, default=2,
                        help="Validation retries")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--dry-run", action="store_true",
                        help="Print assembled prompt; no Ollama request")
    args = parser.parse_args(argv)
    try:
        if args.retries < 0 or args.timeout <= 0:
            parser.error("--retries must be >= 0 and timeout must be positive")
        raw = (
            sys.stdin.read() if args.context == "-"
            else Path(args.context).read_text(encoding="utf-8")
        )
        context = json.loads(raw)
        prompt = build_strategic_prompt(context)
        if args.dry_run:
            output = prompt
        else:
            decision = generate_decision(
                context, model=args.model, host=args.host,
                retries=args.retries, timeout=args.timeout,
            )
            output = json.dumps(decision, ensure_ascii=False, indent=2)
        if args.output:
            Path(args.output).write_text(output + "\n", encoding="utf-8")
        else:
            print(output)
        return 0
    except (OSError, ValueError, TypeError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"Ollama request failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())