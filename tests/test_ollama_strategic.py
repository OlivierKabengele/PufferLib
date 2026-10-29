import importlib.util
import json
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "ocean" / "nmmo3" / "ollama-strategic.py"
sys.path.insert(0, str(SCRIPT.parent))
spec = importlib.util.spec_from_file_location("ollama_strategic", SCRIPT)
ollama_strategic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ollama_strategic)


def _decision():
    return {
        "objective": "survive",
        "reason": "No immediate target is visible.",
        "priority": 0.8,
        "horizon_ticks": 25,
        "target": {"kind": "none", "id": 0, "r": 0, "c": 0},
        "subgoals": [
            {"goal": "Maintain a safe position",
             "success_condition": "No immediate threat is visible"}
        ],
        "action_preferences": {
            "attack": 0.1, "harvest": 0.0, "equip": 0.0, "buy": 0.0,
            "sell": 0.0, "explore": 0.2, "retreat": 1.0,
        },
        "replan_if": ["low_hp"],
    }


def _context():
    return {
        "schema_version": 1,
        "tick": 100,
        "player_id": 0,
        "player": {},
        "world": {"height": 10, "width": 10},
        "nearby": {},
        "market": {},
    }


def test_generate_decision_uses_local_ollama_and_retries_invalid_json(monkeypatch):
    monkeypatch.delenv("OLLAMA_STRATEGIC_MODEL", raising=False)
    monkeypatch.delenv("OLLAMA_HOST", raising=False)
    outputs = iter(["[]", json.dumps(_decision())])
    requests = []

    class Response:
        def __init__(self, output):
            self.output = output

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def read(self):
            return json.dumps({
                "message": {"content": self.output},
                "prompt_eval_count": 12,
                "eval_count": 9,
            }).encode()

    def opener(req, timeout):
        requests.append((req, json.loads(req.data), timeout))
        return Response(next(outputs))

    token_counts = []
    result = ollama_strategic.generate_decision(
        _context(), retries=1, client=opener,
        usage_callback=lambda input_count, output_count: token_counts.append(
            (input_count, output_count)
        ),
    )

    assert result == _decision()
    assert len(requests) == 2
    assert requests[0][0].full_url == "http://localhost:11434/api/chat"
    assert requests[0][1]["model"] == "qwen3:4b-thinking"
    assert requests[0][1]["format"] == "json"
    assert requests[0][1]["stream"] is False
    assert "previous response failed validation" in requests[1][1]["messages"][0]["content"]
    assert token_counts == [(12, 9), (12, 9)]


def test_generate_decision_rejects_invalid_retry_and_timeout():
    with pytest.raises(ValueError, match="Invalid retries or timeout"):
        ollama_strategic.generate_decision(_context(), retries=-1)


def test_generate_decision_respects_ollama_environment(monkeypatch):
    monkeypatch.setenv("OLLAMA_STRATEGIC_MODEL", "custom-model")
    monkeypatch.setenv("OLLAMA_HOST", "http://ollama.internal:1234/")
    seen = []

    class Response:
        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def read(self):
            return json.dumps({"message": {"content": json.dumps(_decision())}}).encode()

    def opener(req, timeout):
        seen.append((req.full_url, json.loads(req.data)["model"]))
        return Response()

    ollama_strategic.generate_decision(_context(), client=opener)
    assert seen == [("http://ollama.internal:1234/api/chat", "custom-model")]