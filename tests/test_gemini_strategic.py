import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ocean" / "nmmo3"))

import gemini_strategic


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


def test_generate_decision_parses_and_retries_invalid_json(monkeypatch):
    context = {
        "schema_version": 1,
        "tick": 100,
        "player_id": 0,
        "player": {},
        "world": {"height": 10, "width": 10},
        "nearby": {},
        "market": {},
    }
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
                "candidates": [{
                    "content": {"parts": [{"text": self.output}]},
                    "finishReason": "STOP",
                }]
            }).encode()

    def opener(req, timeout):
        requests.append((json.loads(req.data), timeout))
        return Response(next(outputs))

    monkeypatch.setenv("GEMINI_API_KEY", "test-key")
    result = gemini_strategic.generate_decision(
        context, retries=1, client=opener)

    assert result == _decision()
    assert len(requests) == 2
    assert requests[0][0]["generationConfig"]["responseMimeType"] == "application/json"
    assert "previous response failed validation" in requests[1][0]["contents"][0]["parts"][0]["text"]
