"""Neural MMO 3 LLM planning bridge with strict per-agent 500-tick gating.

CLI:
  python strategic_bridge.py context.json --provider gpt --state-file planner_state.json
  python strategic_bridge.py context.json --provider gemini --state-file planner_state.json
  python strategic_bridge.py context.json --provider ollama --state-file planner_state.json
  python strategic_bridge.py context.json --provider gpt --dry-run

Output is ALWAYS a JSON envelope containing status, decision (or null), and
compact executor fields (or null). A single state file persists agent cooldowns
between process invocations. Use separate state files for independent games.

Important: this module does NOT call C, inject RL observations, or execute actions.
The game host must call this bridge with a current strategic_context_build_json
snapshot, decode the result and install the plan in strategic_executor.c.
"""
from __future__ import annotations

import argparse
import contextlib
import importlib.util
import json
import os
import sys
from pathlib import Path
from typing import Any, Callable

from strategic_prompt import validate_strategic_decision

MIN_LLM_TICKS = 500
OBJECTIVES = ("survive", "train_combat", "harvest", "equip", "trade", "explore")
KINDS = ("none", "enemy", "item", "position", "market")
PREFERENCES = ("attack", "harvest", "equip", "buy", "sell", "explore", "retreat")


def executor_payload(decision: dict[str, Any]) -> dict[str, Any]:
    """Convert validated decision into numeric SE strategic plan fields."""
    return {
        "objective": OBJECTIVES.index(decision["objective"]),
        "kind": KINDS.index(decision["target"]["kind"]),
        "target_id": decision["target"]["id"],
        "target_r": decision["target"]["r"],
        "target_c": decision["target"]["c"],
        "horizon_ticks": decision["horizon_ticks"],
        "priority": decision["priority"],
        "preferences": [decision["action_preferences"][key] for key in PREFERENCES],
        "replan_on_low_hp": int("low_hp" in decision["replan_if"]),
        "replan_on_target_lost": int("target_disappears" in decision["replan_if"]),
        "replan_on_inventory_full": int("inventory_full" in decision["replan_if"]),
        "subgoal_count": len(decision["subgoals"]),
    }


def _load_state(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"version": 1, "agents": {}}
    obj = json.loads(path.read_text(encoding="utf-8"))
    if obj.get("version") != 1 or not isinstance(obj.get("agents"), dict):
        raise ValueError("Unsupported/corrupt bridge state file")
    return obj


def _save_state(path: Path, state: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp." + str(os.getpid()))
    try:
        temp.write_text(json.dumps(state, separators=(",", ":")), encoding="utf-8")
        os.replace(temp, path)
    finally:
        if temp.exists():
            temp.unlink()


@contextlib.contextmanager
def _locked(path: Path):
    """Process-level mutex on POSIX; reject unsupported platforms."""
    try:
        import fcntl
    except ImportError as exc:
        raise RuntimeError("State-file locking requires POSIX fcntl; use a single-process orchestrator on Windows") from exc
    lock_file = path.with_name(path.name + ".lock")
    lock_file.parent.mkdir(parents=True, exist_ok=True)
    with lock_file.open("a+") as f:
        fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)


def _make_request(context: dict, provider: str, model: str | None, usage_callback=None) -> dict:
    if provider == "gpt":
        from gpt_strategic import generate_decision
    elif provider == "gemini":
        from gemini_strategic import generate_decision
    elif provider == "ollama":
        script = Path(__file__).with_name("ollama-strategic.py")
        spec = importlib.util.spec_from_file_location("ollama_strategic", script)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"Cannot load Ollama planner: {script}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        generate_decision = module.generate_decision
    else:
        raise ValueError("provider must be gpt, gemini, or ollama")
    opts = {"retries": 0, "usage_callback": usage_callback}
    if model:
        opts["model"] = model
    return generate_decision(context, **opts)


def plan_if_due(context: dict, state_file: str | Path, *, provider: str = "gpt",
                model: str | None = None,
                request_fn: Callable[[dict, str, str | None], dict] | None = None,
                force_replan: bool = False, usage_callback=None) -> dict:
    """Return planned/cooldown response. Enforces >=500 ticks even on failures.

    `force_replan` means a new plan is desired, NOT a cooldown bypass. For an
    invocation with no force_replan, also attempts planning when 500 ticks elapsed.
    Store state in a fresh path for each new episode (or reset externally).
    """
    if not isinstance(context, dict) or context.get("schema_version") != 1:
        raise ValueError("Expected strategic context schema_version=1")
    for k in ("player", "world", "nearby", "market"):
        if not isinstance(context.get(k), dict):
            raise ValueError(f"Missing context object: {k}")
    tick, player = context.get("tick"), context.get("player_id")
    if type(tick) is not int or tick < 0 or type(player) is not int or player < 0:
        raise ValueError("tick and player_id must be nonnegative integers")
    if provider not in ("gpt", "gemini", "ollama"):
        raise ValueError("provider must be gpt, gemini, or ollama")
    path = Path(state_file)
    with _locked(path):
        state = _load_state(path)
        key = str(player)
        previous = state["agents"].get(key, {})
        last = previous.get("last_llm_tick")
        if last is not None and (type(last) is not int or tick < last):
            raise ValueError("Tick went backward: use a new state file for a new episode")
        remaining = max(0, MIN_LLM_TICKS - (tick - last)) if last is not None else 0
        if remaining:
            return {"status": "cooldown", "tick": tick, "player_id": player,
                    "last_llm_tick": last, "ticks_until_next_request": remaining,
                    "replan_pending": bool(force_replan), "decision": None, "executor_plan": None}
        # Write consumed request tick before invoking the provider, so failures
        # (and process crashes) cannot defeat the cooldown.
        state["agents"][key] = {"last_llm_tick": tick}
        _save_state(path, state)
        try:
            if request_fn is None:
                decision = _make_request(context, provider, model, usage_callback)
            else:
                decision = request_fn(context, provider, model)
            decision = validate_strategic_decision(decision, context)
        except Exception as exc:
            return {"status": "error", "tick": tick, "player_id": player,
                    "last_llm_tick": tick, "ticks_until_next_request": MIN_LLM_TICKS,
                    "replan_pending": True, "decision": None, "executor_plan": None,
                    "error": f"{type(exc).__name__}: {exc}"}
        return {"status": "planned", "tick": tick, "player_id": player,
                "last_llm_tick": tick, "ticks_until_next_request": MIN_LLM_TICKS,
                "replan_pending": False, "decision": decision,
                "executor_plan": executor_payload(decision)}




def write_compact_plan(path: str, result: dict) -> None:
    """Write numeric C-readable executor plan or one-word status."""
    p = result.get("executor_plan")
    if result.get("status") != "planned" or p is None:
        Path(path).write_text(str(result.get("status", "error")) + "\n", encoding="utf-8")
        return
    values = [p["objective"], p["kind"], p["target_id"], p["target_r"],
              p["target_c"], p["horizon_ticks"], p["priority"],
              *p["preferences"], p["replan_on_low_hp"],
              p["replan_on_target_lost"], p["replan_on_inventory_full"],
              p["subgoal_count"]]
    Path(path).write_text("planned " + " ".join(map(str, values)) + "\n", encoding="utf-8")

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("context", help="JSON file or '-' for stdin")
    p.add_argument("--provider", choices=("gpt", "gemini", "ollama"), default="gpt")
    p.add_argument("--model", default=None)
    p.add_argument("--state-file", default="strategic_bridge_state.json")
    p.add_argument("--output", "-o", default=None)
    p.add_argument("--compact-output", default=None, help="Write C-readable plan for the integrated demo")
    p.add_argument("--usage-output", default=None, help="Write per-request API token counts as two integers")
    p.add_argument("--force-replan", action="store_true", help="Mark replanning need without bypassing cooldown")
    p.add_argument("--dry-run", action="store_true", help="Inspect cooldown without a provider request or state mutation")
    args = p.parse_args(argv)
    try:
        raw = sys.stdin.read() if args.context == "-" else Path(args.context).read_text(encoding="utf-8")
        context = json.loads(raw)
        if args.dry_run:
            tick = context["tick"]
            state = _load_state(Path(args.state_file))
            last = state["agents"].get(str(context["player_id"]), {}).get("last_llm_tick")
            if last is not None and tick < last:
                raise ValueError("Tick went backward: use a new state file")
            result = {"status": "ready" if last is None or tick-last >= MIN_LLM_TICKS else "cooldown",
                      "tick": tick, "player_id": context["player_id"],
                      "last_llm_tick": last, "ticks_until_next_request": 0 if last is None else max(0,MIN_LLM_TICKS-(tick-last)),
                      "decision": None, "executor_plan": None}
        else:
            usage_totals = [0, 0]
            def on_usage(inp, out):
                usage_totals[0] += int(inp)
                usage_totals[1] += int(out)
            try:
                result = plan_if_due(context, args.state_file, provider=args.provider,
                                     model=args.model, force_replan=args.force_replan,
                                     usage_callback=on_usage)
            finally:
                if args.usage_output:
                    Path(args.usage_output).write_text(
                        f"{usage_totals[0]} {usage_totals[1]}\n", encoding="utf-8")
        if args.compact_output:
            write_compact_plan(args.compact_output, result)
        output = json.dumps(result, indent=2, ensure_ascii=False)
        if args.output:
            Path(args.output).write_text(output+"\n", encoding="utf-8")
        else:
            print(output)
        return 1 if result["status"] == "error" else 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"Bridge error: {exc}",file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
