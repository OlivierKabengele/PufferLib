# PufferLib train command integration (Neural MMO 3)

Replace the corresponding files in your existing project with files from this archive.

- Replace `pufferlib/pufferl.py` (or the actual pufferl.py providing the `puffer` entrypoint).
- Replace your compiled nmmo3 source `binding.c`, and update `strategic_executor.c`.
- Keep the strategic context/prompt, provider planner and bridge files beside each other in `resources/nmmo3/` or supply `--strategy-bridge-path`.
- Rebuild the native PufferLib extension using the repository's normal build procedure **after updating binding.c**. Otherwise the old compiled backend will not call the strategic planner.

Run:

    puffer train nmmo3 --gpt-strategy
    puffer train nmmo3 --gemini-strategy
    puffer train nmmo3 --ollama-strategy
    puffer train nmmo3 --gpt-strategy --strategy-influence 20
    puffer train nmmo3 --gemini-strategy --strategy-influence 0

Provider flags are mutually exclusive. `puffer train nmmo3` stays baseline. CLI selects provider, bridge location and Python executable; you do NOT need to export NMMO_STRATEGIC_* variables. GPT and Gemini credentials must be supplied through their normal authentication flow (`OPENAI_API_KEY` / `GEMINI_API_KEY`). GPT requires the `openai` package; Gemini and Ollama use Python's standard library. Ollama requires a running local server at `http://localhost:11434` by default, with model `qwen3:4b-thinking`; set `OLLAMA_HOST` or `OLLAMA_STRATEGIC_MODEL` to override these defaults. The selected model must already be available to the Ollama server.

**Architecture and caveats**

The native `c_step()` wrapper runs the existing `strategic_executor.c` before the ordinary environment step, so decisions affect the actions used during native PufferLib training. Actions remain a 26-way space; observations remain size 1707. A strategy only affects target-directed movement for the configured influence probability. This is RL training guided by a GPT, Gemini, or local Ollama planner; it does NOT fine-tune the language model.

To bound API costs and avoid one call per agent/env every 500 ticks, the implementation runs the planner only for player 0 in the **first reset environment per native worker**. All other agents/environments run regular RL. Only these selected samples receive guidance, but all remain part of RL rollouts. This is an experimental integration; independent worker processes may each make requests. The native rollout thread blocks while the subprocess calls the LLM. It will slow training and is NOT an asynchronous distributed inference architecture.

500 ticks is enforced per executor on each request attempt, including failure. For the selected environment, episode resets preserve the LLM cooldown using a cumulative tick counter. Requests cannot occur less than 500 cumulative steps after a previous attempt, even when the game episode resets. This does not enforce a global cross-worker request budget.

This packaging has not been compiled with your actual `vecenv.h` / PufferLib native extension or tested against real provider APIs. The native wrapper assumes `vecenv.h` invokes `c_reset`, `c_step`, and `c_close`, as in the standard backend. Test on your installed PufferLib version and inspect the action-count logs.
