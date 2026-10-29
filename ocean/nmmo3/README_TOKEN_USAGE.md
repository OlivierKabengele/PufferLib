# LLM token metrics for PufferLib + W&B

Use the existing commands:

    puffer train nmmo3 --gpt-strategy
    puffer train nmmo3 --gemini-strategy

Replace the corresponding source files with those in this package and rebuild the native PufferLib extension. The Gemini client uses urllib, not google-genai. Existing API key environment variables remain necessary.

Provider clients print per-response `[strategy_tokens] Input_tokens=... Output_tokens=...` on stderr. No extra metadata is added to the strategic decision JSON.

The bridge accepts `--usage-output PATH` for a two-integer sidecar (`input output`), aggregating provider responses for that invocation. The native executor reads it and publishes via binding.c's `my_log_extra()`:

- `Input_tokens` — cumulative prompt/input tokens, per strategy runtime
- `Output_tokens` — cumulative completion/candidate tokens, per strategy runtime
- `LLM_last_input_tokens` and `LLM_last_output_tokens` — latest request's usage; can be plotted as samples, but sampled only when W&B logging occurs
- `strategy_decisions` — successfully installed plans
- `strategy_updates` — bridge invocations (including failures)
- `strategy_failure` — unsuccessful plan installations

PufferLib's existing `wandb.log(flat_logs, step=...)` path can log these values without direct wandb imports in provider clients. Prefixing may occur depending on `my_log_extra` routing.

These are reported API usage fields, NOT estimates. Gemini's `candidatesTokenCount` does not necessarily include hidden thinking tokens; total billed usage can differ. The metrics count all provider responses that returned usage metadata, including validation retries. Network failures without usable metadata cannot be measured. In multi-worker training, `my_log_extra` will expose counters from that process's selected strategy runtime; it does not aggregate other OS processes. Cumulative counters and last-request sampling are not a complete per-call histogram. For exact per-call distributions, persist an event log in the host and call `wandb.log` for each event.

Native compilation, real API calls and W&B runs have not been performed in this container.
