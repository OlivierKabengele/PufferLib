"""Google Gemini-powered Neural MMO 3 strategic planner.

No third-party Gemini SDK required. Uses Python standard-library HTTPS.
Set:     export GEMINI_API_KEY='...'
Run:     python gemini_strategic.py context.json --output decision.json
         cat context.json | python gemini_strategic.py -
         python gemini_strategic.py context.json --dry-run

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

from strategic_prompt import build_strategic_prompt, parse_strategic_decision

DEFAULT_MODEL = "gemini-3.5-flash-lite"


def _gemini_rest(prompt: str, *, model: str, max_output_tokens: int,
                 timeout: float, api_key: str, opener=None, usage_callback=None) -> str:
    """Call Gemini generateContent over HTTPS, returning candidate text."""
    if not model or "/" in model or ":" in model or "?" in model:
        raise ValueError("Invalid Gemini model name")
    url = ("https://generativelanguage.googleapis.com/v1beta/models/"
           + model + ":generateContent")
    payload = {
        "contents": [{"role": "user", "parts": [{"text": prompt}]}],
        "generationConfig": {
            "responseMimeType": "application/json",
            "maxOutputTokens": max_output_tokens,
            "temperature": 0.2,
        },
    }
    req = request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json", "x-goog-api-key": api_key},
    )
    transport = opener or request.urlopen
    try:
        with transport(req, timeout=timeout) as response:
            data = json.loads(response.read().decode("utf-8"))
    except error.HTTPError as exc:
        # Do not expose credentials (the key is only sent in the header).
        detail = exc.read(4096).decode("utf-8", errors="replace")
        try:
            detail = json.loads(detail).get("error", {}).get("message", detail)
        except (ValueError, AttributeError):
            pass
        raise RuntimeError(f"Gemini API HTTP {exc.code}: {detail}") from exc
    except error.URLError as exc:
        raise RuntimeError(f"Gemini network error: {exc.reason}") from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError("Gemini returned a non-JSON API response") from exc

    usage = data.get("usageMetadata") or {}
    if usage:
        input_tokens = int(usage.get("promptTokenCount", 0) or 0)
        output_tokens = int(usage.get("candidatesTokenCount", 0) or 0)
        if usage_callback is not None:
            usage_callback(input_tokens, output_tokens)
        print(f"[strategy_tokens] Input_tokens={input_tokens} Output_tokens={output_tokens} model={model}", file=sys.stderr)
    candidates = data.get("candidates") or []
    if not candidates:
        raise RuntimeError("Gemini produced no candidates: " +
                           json.dumps(data.get("promptFeedback", {})))
    candidate = candidates[0]
    parts = candidate.get("content", {}).get("parts", [])
    text = "".join(part.get("text", "") for part in parts if isinstance(part, dict))
    if not text:
        raise RuntimeError("Gemini returned no text (finishReason=" +
                           str(candidate.get("finishReason", "unknown")) + ")")
    return text


def generate_decision(context: dict, *, model: str = DEFAULT_MODEL,
                      max_output_tokens: int = 2000, retries: int = 2,
                      timeout: float = 120.0, client=None, usage_callback=None) -> dict:
    """Return a contract-validated decision. No third-party SDK required.

    client is an optional test transport callable accepting (Request, timeout).
    Normal use needs no client, only the GEMINI_API_KEY environment variable.
    """
    api_key = os.getenv("GEMINI_API_KEY") or os.getenv("GOOGLE_API_KEY")
    if not api_key:
        raise RuntimeError("GEMINI_API_KEY or GOOGLE_API_KEY is not set")
    if retries < 0 or max_output_tokens < 1 or timeout <= 0:
        raise ValueError("Invalid retries, token limit, or timeout")
    prompt = build_strategic_prompt(context)
    last_error = None
    for attempt in range(retries + 1):
        contents = prompt
        if last_error is not None:
            contents += ("\n\nA previous response failed validation: " + last_error +
                         "\nReturn a corrected decision using only observed targets.")
        raw = _gemini_rest(
            contents, model=model, max_output_tokens=max_output_tokens,
            timeout=timeout, api_key=api_key, opener=client,
            usage_callback=usage_callback)
        try:
            return parse_strategic_decision(raw, context)
        except (ValueError, TypeError) as exc:
            last_error = str(exc)
            print(f"Validation failed (attempt {attempt + 1}/{retries + 1}): "
                  f"{last_error}", file=sys.stderr)
    raise RuntimeError(
        f"No valid strategic decision after {retries + 1} attempts: {last_error}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("context", help="Context JSON path or '-' for stdin")
    parser.add_argument("--output", "-o", help="Decision JSON file; defaults to stdout")
    parser.add_argument("--model", default=os.getenv("GEMINI_STRATEGIC_MODEL", DEFAULT_MODEL))
    parser.add_argument("--max-output-tokens", type=int, default=2000)
    parser.add_argument("--retries", type=int, default=2,
                        help="Validation retries")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--dry-run", action="store_true",
                        help="Print assembled prompt; no API call")
    args = parser.parse_args(argv)
    try:
        if args.retries < 0 or args.max_output_tokens < 1 or args.timeout <= 0:
            parser.error("--retries must be >= 0; token limit and timeout must be positive")
        raw = (sys.stdin.read() if args.context == "-"
               else Path(args.context).read_text(encoding="utf-8"))
        context = json.loads(raw)
        prompt = build_strategic_prompt(context)
        if args.dry_run:
            output = prompt
        else:
            decision = generate_decision(
                context, model=args.model, max_output_tokens=args.max_output_tokens,
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