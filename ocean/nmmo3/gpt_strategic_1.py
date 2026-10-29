"""OpenAI-powered Neural MMO 3 strategic planner.

Install: pip install -U openai
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
from pathlib import Path

from strategic_prompt import build_strategic_messages, parse_strategic_decision

DEFAULT_MODEL = "gpt-5.4-nano"


def generate_decision(context: dict, *, model: str = DEFAULT_MODEL,
                      max_output_tokens: int = 2000, retries: int = 2,
                      timeout: float = 120.0, client=None, usage_callback=None) -> dict:
    """Return a contract-validated high-level decision for one snapshot."""
    if client is None:
        if not os.environ.get("OPENAI_API_KEY"):
            raise RuntimeError("OPENAI_API_KEY is not set")
        try:
            from openai import OpenAI
        except ImportError as exc:
            raise RuntimeError("Install the OpenAI SDK: pip install -U openai") from exc
        client = OpenAI(timeout=timeout, max_retries=2)

    messages = build_strategic_messages(context)
    error = None
    for attempt in range(retries + 1):
        input_messages = [dict(m) for m in messages]
        if error is not None:
            input_messages.append({
                "role": "user", "content": (
                    "The previous response was rejected by the decision validator: "
                    f"{error}. Return a fresh single JSON decision matching the "
                    "contract, with all targets grounded in the snapshot."
                )
            })
        response = client.responses.create(
            model=model,
            input=input_messages,
            text={"format": {"type": "json_object"}},
            max_output_tokens=max_output_tokens,
        )
        usage = getattr(response, "usage", None)
        if usage is not None:
            input_tokens = int(getattr(usage, "input_tokens", 0) or 0)
            output_tokens = int(getattr(usage, "output_tokens", 0) or 0)
            if usage_callback is not None:
                usage_callback(input_tokens, output_tokens)
            print(f"[strategy_tokens] Input_tokens={input_tokens} Output_tokens={output_tokens} model={model}", file=sys.stderr)
        raw = getattr(response, "output_text", None)
        if not raw:
            error = f"Empty model output (status={getattr(response, 'status', 'unknown')})"
            continue
        try:
            return parse_strategic_decision(raw, context)
        except (ValueError, TypeError) as exc:
            error = str(exc)
            print(f"Validation failed (attempt {attempt + 1}/{retries + 1}): {error}", file=sys.stderr)
    raise RuntimeError(f"No valid strategic decision after {retries + 1} attempts: {error}")


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
