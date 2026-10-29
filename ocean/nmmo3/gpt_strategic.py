"""OpenAI-powered Neural MMO 3 strategic planner.

No third-party OpenAI SDK required. Uses Python standard-library HTTPS.
Set:     export OPENAI_API_KEY='...'
Run:     python gpt_strategic.py context.json --output decision.json
         cat context.json | python gpt_strategic.py -
         python gpt_strategic.py context.json --dry-run

Requires strategic_prompt.py alongside this script. Writes only validated decision
JSON to stdout (or --output); logs and errors go to stderr.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from urllib import request, error
from pathlib import Path

from strategic_prompt import build_strategic_messages, parse_strategic_decision

DEFAULT_MODEL = "gpt-5.4-nano"


def _openai_rest(messages: list[dict], *, model: str, max_output_tokens: int,
                 timeout: float, api_key: str, opener=None) -> dict:
    """Return raw Responses API JSON. An injected opener enables offline tests."""
    body = json.dumps({
        "model": model,
        "input": messages,
        "text": {"format": {"type": "json_object"}},
        "max_output_tokens": max_output_tokens,
    }).encode("utf-8")
    req = request.Request(
        "https://api.openai.com/v1/responses", data=body, method="POST",
        headers={"Authorization": "Bearer " + api_key,
                 "Content-Type": "application/json"},
    )
    try:
        with (opener or request.urlopen)(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except error.HTTPError as exc:
        detail = exc.read(4096).decode("utf-8", errors="replace")
        try:
            detail = json.loads(detail).get("error", {}).get("message", detail)
        except (ValueError, AttributeError):
            pass
        raise RuntimeError(f"OpenAI API HTTP {exc.code}: {detail}") from exc
    except error.URLError as exc:
        raise RuntimeError(f"OpenAI network error: {exc.reason}") from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError("OpenAI returned invalid JSON") from exc


def _response_text(data: dict) -> str:
    """Extract text from Responses API output items (no SDK output_text helper)."""
    chunks = []
    for item in data.get("output", []):
        if item.get("type") != "message":
            continue
        for part in item.get("content", []):
            if part.get("type") == "output_text":
                chunks.append(part.get("text", ""))
    return "".join(chunks)


def generate_decision(context: dict, *, model: str = DEFAULT_MODEL,
                      max_output_tokens: int = 2000, retries: int = 2,
                      timeout: float = 120.0, client=None, usage_callback=None) -> dict:
    """Validated decision, with usage callback for every API response (even retries).

    client is an optional urllib-compatible opener for tests.
    """
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        raise RuntimeError("OPENAI_API_KEY is not set")
    if retries < 0 or max_output_tokens < 1 or timeout <= 0:
        raise ValueError("Invalid retries, token limit, or timeout")
    messages = build_strategic_messages(context)
    validation_error = None
    for attempt in range(retries + 1):
        current = [dict(m) for m in messages]
        if validation_error is not None:
            current.append({"role": "user", "content":
                "The previous response failed JSON validation: " + validation_error +
                ". Return one corrected JSON decision grounded in the snapshot."})
        response = _openai_rest(current, model=model,
                                max_output_tokens=max_output_tokens,
                                timeout=timeout, api_key=api_key, opener=client)
        usage = response.get("usage") or {}
        if usage:
            input_tokens = int(usage.get("input_tokens") or 0)
            output_tokens = int(usage.get("output_tokens") or 0)
            if usage_callback is not None:
                usage_callback(input_tokens, output_tokens)
            print(f"[strategy_tokens] Input_tokens={input_tokens} "
                  f"Output_tokens={output_tokens} model={model}", file=sys.stderr)
        raw = _response_text(response)
        if not raw:
            validation_error = "Empty model output (status=" + str(response.get("status", "unknown")) + ")"
            continue
        try:
            return parse_strategic_decision(raw, context)
        except (ValueError, TypeError) as exc:
            validation_error = str(exc)
            print(f"Validation failed (attempt {attempt + 1}/{retries + 1}): "
                  f"{validation_error}", file=sys.stderr)
    raise RuntimeError(f"No valid strategic decision after {retries + 1} "
                       f"attempts: {validation_error}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("context", help="Context JSON path or '-' for stdin")
    parser.add_argument("--output", "-o", help="Decision JSON file; defaults to stdout")
    parser.add_argument("--model", default=os.getenv("OPENAI_STRATEGIC_MODEL", DEFAULT_MODEL))
    parser.add_argument("--max-output-tokens", type=int, default=2000)
    parser.add_argument("--retries", type=int, default=2, help="Validation retries (not SDK network retries)")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--dry-run", action="store_true", help="Print assembled messages; no API call")
    args = parser.parse_args(argv)
    try:
        if args.retries < 0 or args.max_output_tokens < 1 or args.timeout <= 0:
            parser.error("--retries must be >= 0; token limit and timeout must be positive")
        raw = sys.stdin.read() if args.context == "-" else Path(args.context).read_text(encoding="utf-8")
        context = json.loads(raw)
        messages = build_strategic_messages(context)  # validates context even in dry-run
        if args.dry_run:
            output = json.dumps(messages, ensure_ascii=False, indent=2)
        else:
            decision = generate_decision(context, model=args.model,
                                         max_output_tokens=args.max_output_tokens,
                                         retries=args.retries, timeout=args.timeout)
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
        print(f"Provider request failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
