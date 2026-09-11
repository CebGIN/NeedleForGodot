# Needle for Godot — GDExtension

A Godot 4.x GDExtension wrapping the [Cactus Needle 2](https://huggingface.co/Cactus-Compute/needle2) "agentic" LLM (45M params, ~14 MB) for tool-calling directly from GDScript.

## Installation

### 1. Copy files into your project

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

### 2. The `.gdextension` file

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
    needle.set_tools("YOUR JSON TOOLS DECLARATION")

    # Connect signals
    needle.loaded.connect(_on_loaded)
    needle.failed.connect(_on_failed)

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

#### `reset() -> void`

Reset the conversation state. Clears the KV cache and conversation history. Tools and model remain loaded.

```gdscript
needle.reset()
```

#### `is_model_loaded() -> bool`

Returns `true` if the model has been loaded successfully.

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

For text responses, `function_calls` is empty and the raw text may be in `reasoning`.

## Performance Tuning

### KV Window

The `kv_window` property controls how many recent tokens the model attends to. **Lower values are faster** for tool-calling because the model generates short responses, tune it according to the hardware you are aiming and speeds needed:

```gdscript
needle.kv_window = 16   # Fast (100–170 ms)
needle.kv_window = 256  # Default (200–330 ms per query)
```
ms times are examples on my laptop while testing.

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

Shorter tool descriptions = faster prefill.

```gdscript
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
NeedleAgent
├── load_model()     → dlopen(libneedle.so) → needle_load(bytes)
├── set_tools()      → needle_init(prompt, tools_json)
├── complete()       → needle_complete(text, max_tokens, buffer, size)
└── reset()          → needle_reset()
```

The extension uses `dlopen`/`dlsym` to load Needle at runtime — no compile-time dependency on the engine library. The model is loaded into memory, then the buffer is freed (the engine copies internally).

### Thread Safety

- `api_mutex` protects all Needle C API calls
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
