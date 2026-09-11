# Needle for Godot — GDExtension

A Godot 4.x GDExtension wrapping the [Cactus Needle 2](https://huggingface.co/Cactus-Compute/needle2) agentic LLM (45M params, ~14 MB) for tool-calling directly from GDScript.

## Requirements

- **Godot** 4.1 or later (tested on 4.7.2)
- **libneedle.so** (Linux), **needle.dll** (Windows), or **libneedle.dylib** (macOS) — downloaded from HuggingFace
- **needle2.cact** — model weights (~14 MB)

## Installation

### 1. Download the engine and model

```bash
pip install cactus-needle
needle fetch --generation 2 --out ./addons/needle_for_godot/bin/linux/
```

Or with `huggingface_hub`:

```bash
pip install huggingface_hub
huggingface-cli download Cactus-Compute/needle2 needle2.cact --local-dir ./addons/needle_for_godot/models/
huggingface-cli download Cactus-Compute/needle2 libneedle.so --local-dir ./addons/needle_for_godot/bin/linux/
```

> **Note:** These commands assume you're running from inside your Godot project directory (the one with `project.godot`).

### 2. Copy files into your project

```
your_project/
├── addons/
│   └── needle_for_godot/
│       ├── needle_for_godot.gdextension
│       ├── LICENSE_NEEDLE
│       ├── icons/
│       │   └── needle_agent.svg
│       ├── bin/
│       │   └── linux/
│       │       ├── libneedle.so
│       │       └── libneedle_for_godot.linux.template_debug.x86_64.so
│       └── models/
│           └── needle2.cact
└── project.godot
```

### 3. The `.gdextension` file

The extension descriptor maps platform-specific builds (relative to the addon folder):

```ini
[configuration]
entry_symbol = "needle_library_init"
compatibility_minimum = "4.1"
reloadable = true

[libraries]
linux.editor.x86_64 = "./bin/linux/libneedle_for_godot.linux.template_debug.x86_64.so"
linux.debug.x86_64 = "./bin/linux/libneedle_for_godot.linux.template_debug.x86_64.so"
linux.release.x86_64 = "./bin/linux/libneedle_for_godot.linux.template_release.x86_64.so"
```

## Quick Start

```gdscript
extends Node

var needle: NeedleAgent

func _ready():
    needle = NeedleAgent.new()

    # Define tools (must be set before load_model)
    needle.set_tools(JSON.stringify([{
        "name": "get_weather",
        "description": "Weather",
        "parameters": {
            "type": "object",
            "properties": {
                "city": {"type": "string"}
            },
            "required": ["city"]
        }
    }]))

    # Connect signals
    needle.loaded.connect(_on_loaded)
    needle.failed.connect(_on_failed)
    needle.completed.connect(_on_completed)

    add_child(needle)
    needle.load_model("res://addons/needle_for_godot/models/needle2.cact")

func _on_loaded():
    var result = needle.complete("What is the weather in London?")
    print(result)

func _on_failed(error: String):
    print("Error: ", error)

func _on_completed(result: Dictionary):
    print("Completed: ", JSON.stringify(result))
```

## API Reference

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `model_path` | String | `""` | Path to `.cact` model file. Set before `load_model()`. |
| `system_prompt` | String | `""` | System prompt prepended to every query. |
| `max_new_tokens` | int | `256` | Maximum tokens to generate per response (1–1024). |
| `auto_download` | bool | `true` | If true, `_ready()` auto-searches for model/library. |
| `kv_window` | int | `256` | KV cache window size. Lower = faster (16 recommended for tool-calling). |
| `threads` | int | `0` | Thread count. 0 = auto-detect. |

### Methods

#### `load_model(path: String) -> void`

Load the Needle library and model from a file path. Emits `loaded` or `failed`.

```gdscript
needle.load_model("res://addons/needle_for_godot/models/needle2.cact")
```

The path can be:
- `res://` path (project-relative)
- Absolute filesystem path

#### `set_tools(tools: String) -> void`

Define available tools as a JSON string. Each tool follows the [OpenAI function-calling format](https://platform.open/docs/api-reference/chat/create#chat-create-tools):

```gdscript
needle.set_tools(JSON.stringify([
    {
        "name": "get_weather",
        "description": "Get weather for a city",
        "parameters": {
            "type": "object",
            "properties": {
                "city": {"type": "string", "description": "City name"}
            },
            "required": ["city"]
        }
    }
]))
```

- If the model is already loaded, this reinitializes with the new tools immediately.
- Passing an empty string is a no-op.

#### `get_tools() -> String`

Returns the currently defined tools as a JSON string.

#### `complete(text: String) -> Dictionary`

**Synchronous** query. Blocks the current thread until the response is ready.

```gdscript
var result = needle.complete("What is the weather in London?")
```

Returns a Dictionary with the engine's JSON response (see [Response Format](#response-format) below).

#### `query(text: String) -> void`

**Asynchronous** query. Runs inference on a background thread. Emits `completed` when done.

```gdscript
needle.query("What is the weather in London?")

func _on_completed(result: Dictionary):
    print(result)
```

Only one query can run at a time. If a query is already in progress, it waits for the previous one to finish.

#### `reset() -> void`

Reset the conversation state. Clears the KV cache and conversation history. Tools and model remain loaded.

```gdscript
needle.reset()
```

#### `is_model_loaded() -> bool`

Returns `true` if the model has been loaded successfully.

#### `get_needle_memory_bytes() -> int64`

Returns the Needle engine's RSS memory contribution in bytes (Linux only). Returns 0 on other platforms.

```gdscript
var mem_mb = needle.get_needle_memory_bytes() / (1024 * 1024)
print("Needle using %d MB" % mem_mb)
```

### Signals

| Signal | Parameters | Description |
|--------|-----------|-------------|
| `loaded` | — | Emitted when the model is loaded and ready. |
| `failed` | `error: String` | Emitted on any error (load, init, or query failure). |
| `completed` | `result: Dictionary` | Emitted when an async `query()` finishes. Emitted on the main thread in `_process()`. |
| `download_progress` | `progress: Float` | Emitted during download (0.0 to 1.0). Not yet implemented. |

## Response Format

The engine returns a JSON object. After parsing, the Dictionary contains:

```json
{
    "type": "call",
    "success": true,
    "confidence": 1.0,
    "function_calls": [
        {
            "name": "get_weather",
            "arguments": {"city": "London"}
        }
    ],
    "decode_tps": 222.7,
    "prefill_tps": 290.0,
    "peak_ram_mb": 123.7,
    "validation": {"negation": false, "ungrounded": []}
}
```

Key fields:
- `type`: `"call"` (tool call), `"text"` (text response), or `"error"`
- `function_calls`: Array of tool calls, each with `name` and `arguments` (Dictionary)
- `success`: Whether the engine successfully generated a valid response
- `confidence`: Model's confidence in the output (0.0–1.0)
- `decode_tps` / `prefill_tps`: Inference speed metrics (tokens/sec)

For text responses, `function_calls` is empty and the raw text may be in `reasoning` or other fields depending on the engine version.

## Performance Tuning

### KV Window

The `kv_window` property controls how many recent tokens the model attends to. **Lower values are faster** for tool-calling because the model generates short responses:

```gdscript
needle.kv_window = 16   # Fast (100–170 ms per query)
# needle.kv_window = 256  # Default (200–330 ms per query)
```

Benchmark results (5 tools, `kv_window=16`):

| Tools | Avg Query Time |
|-------|---------------|
| 1 | ~170 ms |
| 5 | ~270 ms |
| 10 | ~830 ms |
| 20 | ~940 ms |

### Threads

The `threads` property controls Needle's internal thread pool. Default (0) auto-detects. Most systems perform best with the default:

```gdscript
needle.threads = 0   # Auto (recommended)
```

### Tool Descriptions

Shorter tool descriptions = faster prefill. Use concise names:

```gdscript
# Fast
"description": "Weather"

# Slow
"description": "Get the current weather conditions for a specified city"
```

### Reset Between Independent Queries

If queries are independent (not a conversation), call `reset()` between them to keep the KV cache small:

```gdscript
needle.reset()
var result = needle.complete("query 1")

needle.reset()
var result = needle.complete("query 2")
```

For conversations, don't reset — the model uses previous context.

## Architecture

```
NeedleAgent (Node)
├── load_model()     → dlopen(libneedle.so) → needle_load(bytes)
├── set_tools()      → needle_init(prompt, tools_json)
├── complete()       → needle_complete(text, max_tokens, buffer, size) [sync]
├── query()          → needle_complete(...) on worker thread [async]
└── reset()          → needle_reset()
```

The extension uses `dlopen`/`dlsym` to load Needle at runtime — no compile-time dependency on the engine library. The model is loaded into memory, then the buffer is freed (the engine copies internally).

### Thread Safety

- `api_mutex` protects all Needle C API calls
- `query()` runs inference on a `Thread`, with results delivered via `_process()` on the main thread
- `complete()` blocks the calling thread (safe to call from `_ready()` or coroutines)

### Memory

- Needle's RSS contribution: ~5 MB (measured via `/proc/self/statm`)
- Model weights: ~14 MB (copied into engine memory, then freed from GDScript side)
- KV cache: `kv_window * 15,840 bytes/position` (~400 KB at window=16, ~4 MB at window=256)

## Building from Source

```bash
git clone --recursive https://github.com/you/NeedleForGodot.git
cd NeedleForGodot
scons -j$(nproc)
```

Requires:
- `godot-cpp` submodule (branch 4.5 or compatible)
- C++17 compiler with C++17 support
- `dlopen`/`dlsym` (Linux/macOS) or `LoadLibrary` (Windows)

Build output: `demo/addons/needle_for_godot/bin/linux/libneedle_for_godot.linux.template_debug.x86_64.so`

## Troubleshooting

| Error | Cause | Fix |
|-------|-------|-----|
| `"Library not found"` | `libneedle.so/dll` not in search paths | Place in `addons/needle_for_godot/bin/linux/` or set `library_path` |
| `"Model not found"` | `needle2.cact` not found | Place in `addons/needle_for_godot/models/` or set `model_path` |
| `"needle_init failed"` | Tool JSON malformed | Validate your tool definitions |
| `"Model not loaded"` | `query()` called before `load_model()` completes | Wait for `loaded` signal |
| Deadlock / hang | Calling `query()` from `loaded` signal | Use `complete()` in signal handler, or defer `query()` |
| Empty tools skip | `set_tools("")` is a no-op | Pass valid JSON tool array |
