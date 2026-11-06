import contextlib
import io
import json
import os
import sys
import unittest
from unittest import mock
import urllib.error

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "ocean", "nmmo3"))
import ollama_strategy


class OllamaStrategyTests(unittest.TestCase):
    def test_reports_token_counts_and_returns_them(self):
        strategy = {
            "strategy_id": "explore",
            "strategy_parameters": {},
            "risk_parameters": {},
            "social_parameters": {},
            "contingency_parameters": {},
            "confidence": 0.5,
        }
        response_body = json.dumps({
            "prompt_eval_count": 123,
            "eval_count": 45,
            "response": json.dumps(strategy),
        }).encode()
        response = io.BytesIO(response_body)
        stderr = io.StringIO()

        with (
            mock.patch.object(ollama_strategy.urllib.request, "urlopen", return_value=response),
            contextlib.redirect_stderr(stderr),
        ):
            result = ollama_strategy.query_ollama("{}")

        self.assertEqual(result["llm_input_tokens"], 123)
        self.assertEqual(result["llm_output_tokens"], 45)
        self.assertEqual(stderr.getvalue().splitlines(), [
            "Input tokens: 123",
            "Output tokens: 45",
            "Total tokens: 168",
        ])

    def test_reports_request_failures_instead_of_silently_exiting(self):
        stderr = io.StringIO()
        with (
            mock.patch.object(
                ollama_strategy,
                "query_ollama",
                side_effect=urllib.error.URLError("connection refused"),
            ),
            mock.patch.object(ollama_strategy.sys, "stdin", io.StringIO("{}")),
            contextlib.redirect_stderr(stderr),
            self.assertRaises(SystemExit) as raised,
        ):
            ollama_strategy.main()

        self.assertEqual(raised.exception.code, 1)
        self.assertIn("Ollama request failed:", stderr.getvalue())
        self.assertIn("connection refused", stderr.getvalue())
