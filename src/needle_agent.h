#ifndef NEEDLE_AGENT_H
#define NEEDLE_AGENT_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/array.hpp>

#include <mutex>

namespace godot {

class NeedleAgent : public RefCounted {
	GDCLASS(NeedleAgent, RefCounted)

protected:
	static void _bind_methods();

private:
	// Exported properties
	String model_path;
	String system_prompt;
	int max_new_tokens = 256;
	bool auto_download = true;
	int kv_window = 256;
	int threads = 0;

	// Internal state
	bool model_loaded = false;
	bool init_done = false;
	std::mutex api_mutex;

	// Tool definitions stored as JSON string
	String tools_json;

	// Path to the downloaded/cached library
	String library_path;

	// Reusable output buffer (allocated once, not per complete() call)
	static const int OUTPUT_BUFFER_SIZE = 65536;
	char output_buffer[OUTPUT_BUFFER_SIZE];

	// Per-instance library handle and function pointers
	void *lib_handle = nullptr;
	String lib_copy_path;

	typedef int (*needle_load_fn)(const void *, uint64_t);
	typedef int (*needle_init_fn)(const char *, const char *, const char *);
	typedef int (*needle_complete_fn)(const char *, int, char *, int);
	typedef void (*needle_reset_fn)(void);

	needle_load_fn fn_load = nullptr;
	needle_init_fn fn_init = nullptr;
	needle_complete_fn fn_complete = nullptr;
	needle_reset_fn fn_reset = nullptr;

	// Internal methods
	void _copy_and_load_library();
	void _unload_library();
	void _load_model_from_file(const String &p_path);
	bool _try_init_needle(String &r_error);
	String _get_platform_library_name() const;
	String _get_cache_dir() const;

public:
	NeedleAgent();
	~NeedleAgent() override;

	// Property getters/setters
	void set_model_path(const String &p_path);
	String get_model_path() const;

	void set_system_prompt(const String &p_prompt);
	String get_system_prompt() const;

	void set_max_new_tokens(int p_max);
	int get_max_new_tokens() const;

	void set_auto_download(bool p_enabled);
	bool get_auto_download() const;

	void set_kv_window(int p_window);
	int get_kv_window() const;

	void set_threads(int p_threads);
	int get_threads() const;

	void set_tools(const String &p_tools);
	String get_tools() const;

	// Methods exposed to GDScript
	void load_model(const String &p_path);
	Dictionary complete(const String &p_text);
	void reset();

	bool is_model_loaded() const;
};

} // namespace godot

#endif // NEEDLE_AGENT_H
