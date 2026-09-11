#include "needle_agent.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

#ifdef __linux__
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>
#define dlopen_compat dlopen
#define dlsym_compat dlsym
#define dlclose_compat dlclose
#define setenv(name, val, overwrite) setenv(name, val, overwrite)
#endif

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define dlopen_compat LoadLibrary
#define dlsym_compat GetProcAddress
#define dlclose_compat FreeLibrary
#define RTLD_LAZY 0
#define setenv(name, val, overwrite) _putenv_s(name, val)
static void *dlopen_compat_win(const char *path, int) {
  return (void *)LoadLibraryA(path);
}
static void *dlsym_compat_win(void *handle, const char *name) {
  return (void *)GetProcAddress((HMODULE)handle, name);
}
static void dlclose_compat_win(void *handle) { FreeLibrary((HMODULE)handle); }
#undef dlopen_compat
#undef dlsym_compat
#undef dlclose_compat
#define dlopen_compat dlopen_compat_win
#define dlsym_compat dlsym_compat_win
#define dlclose_compat dlclose_compat_win
#endif

using namespace godot;

void NeedleAgent::_bind_methods() {
  // Properties
  ClassDB::bind_method(D_METHOD("get_model_path"), &NeedleAgent::get_model_path);
  ClassDB::bind_method(D_METHOD("set_model_path", "path"), &NeedleAgent::set_model_path);
  ADD_PROPERTY(PropertyInfo(Variant::STRING, "model_path", PROPERTY_HINT_FILE, "*.cact"), "set_model_path", "get_model_path");

  ClassDB::bind_method(D_METHOD("get_system_prompt"), &NeedleAgent::get_system_prompt);
  ClassDB::bind_method(D_METHOD("set_system_prompt", "prompt"), &NeedleAgent::set_system_prompt);
  ADD_PROPERTY(PropertyInfo(Variant::STRING, "system_prompt"), "set_system_prompt", "get_system_prompt");

  ClassDB::bind_method(D_METHOD("get_max_new_tokens"), &NeedleAgent::get_max_new_tokens);
  ClassDB::bind_method(D_METHOD("set_max_new_tokens", "max"), &NeedleAgent::set_max_new_tokens);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "max_new_tokens", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_new_tokens", "get_max_new_tokens");

  ClassDB::bind_method(D_METHOD("get_auto_download"), &NeedleAgent::get_auto_download);
  ClassDB::bind_method(D_METHOD("set_auto_download", "enabled"), &NeedleAgent::set_auto_download);
  ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_download"), "set_auto_download", "get_auto_download");

  ClassDB::bind_method(D_METHOD("get_kv_window"), &NeedleAgent::get_kv_window);
  ClassDB::bind_method(D_METHOD("set_kv_window", "window"), &NeedleAgent::set_kv_window);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "kv_window", PROPERTY_HINT_RANGE, "16,1024,16"), "set_kv_window", "get_kv_window");

  ClassDB::bind_method(D_METHOD("get_threads"), &NeedleAgent::get_threads);
  ClassDB::bind_method(D_METHOD("set_threads", "threads"), &NeedleAgent::set_threads);
  ADD_PROPERTY(PropertyInfo(Variant::INT, "threads", PROPERTY_HINT_RANGE, "0,16,1"), "set_threads", "get_threads");

  // Methods
  ClassDB::bind_method(D_METHOD("set_tools", "tools"), &NeedleAgent::set_tools);
  ClassDB::bind_method(D_METHOD("get_tools"), &NeedleAgent::get_tools);

  ClassDB::bind_method(D_METHOD("load_model", "path"), &NeedleAgent::load_model);
  ClassDB::bind_method(D_METHOD("complete", "text"), &NeedleAgent::complete);
  ClassDB::bind_method(D_METHOD("reset"), &NeedleAgent::reset);
  ClassDB::bind_method(D_METHOD("is_model_loaded"), &NeedleAgent::is_model_loaded);

  // Signals
  ADD_SIGNAL(MethodInfo("loaded"));
  ADD_SIGNAL(MethodInfo("failed", PropertyInfo(Variant::STRING, "error")));
}

NeedleAgent::NeedleAgent() {
  std::memset(output_buffer, 0, OUTPUT_BUFFER_SIZE);
}
NeedleAgent::~NeedleAgent() { _unload_library(); }

// --- Property getters/setters ---
String NeedleAgent::get_model_path() const { return model_path; }
void NeedleAgent::set_model_path(const String &p_path) { model_path = p_path; }

String NeedleAgent::get_system_prompt() const { return system_prompt; }
void NeedleAgent::set_system_prompt(const String &p_prompt) {
  system_prompt = p_prompt;
}

int NeedleAgent::get_max_new_tokens() const { return max_new_tokens; }
void NeedleAgent::set_max_new_tokens(int p_max) { max_new_tokens = p_max; }

bool NeedleAgent::get_auto_download() const { return auto_download; }
void NeedleAgent::set_auto_download(bool p_enabled) { auto_download = p_enabled; }

int NeedleAgent::get_kv_window() const { return kv_window; }
void NeedleAgent::set_kv_window(int p_window) {
  kv_window = p_window;
  if (model_loaded) WARN_PRINT("NeedleAgent: kv_window change ignored (library already loaded)");
}

int NeedleAgent::get_threads() const { return threads; }
void NeedleAgent::set_threads(int p_threads) {
  threads = p_threads;
  if (model_loaded) WARN_PRINT("NeedleAgent: threads change ignored (library already loaded)");
}

static inline bool _validate_json_syntax(const String &p_str) { JSON json; return json.parse(p_str) == OK; }

String NeedleAgent::get_tools() const { return tools_json; }
void NeedleAgent::set_tools(const String &p_tools) {
  if (p_tools.is_empty()) {
    tools_json = "";
    return;
  }

  if (!_validate_json_syntax(p_tools)) {
    String error_msg = "set_tools: Invalid JSON syntax";
    ERR_PRINT("NeedleAgent: " + error_msg);
    emit_signal("failed", error_msg);
    return;
  }

  tools_json = p_tools;

  // If model is already loaded, (re-)initialize with new tools
  if (model_loaded && fn_init) {
    String init_error;
    {
      std::lock_guard<std::mutex> lock(api_mutex);
      _try_init_needle(init_error);
    }
    if (!init_error.is_empty()) {
      emit_signal("failed", init_error);
    }
  }
}

bool NeedleAgent::_try_init_needle(String &r_error) {
  if (!fn_init || tools_json.is_empty()) return true;
  String sys = system_prompt.is_empty() ? "" : system_prompt;
  int rc =
      fn_init(sys.utf8().get_data(), tools_json.utf8().get_data(), nullptr);
  if (rc < 0) {
    r_error = String("needle_init failed with code: ") + String::num_int64(rc);
    return false;
  }
  init_done = true;
  return true;
}

// --- Platform detection ---

String NeedleAgent::_get_platform_library_name() const {
#ifdef _WIN32
  return "needle.dll";
#elif __APPLE__
  return "libneedle.dylib";
#else
  return "libneedle.so";
#endif
}

String NeedleAgent::_get_cache_dir() const {
  return OS::get_singleton()->get_user_data_dir() + "/.cache/needle";
}

// --- Library loading (per-instance via copied .so) ---

void NeedleAgent::_copy_and_load_library() {
  if (lib_handle != nullptr) {
    return; // Already loaded
  }

  // Set Needle environment variables before loading the library
  if (kv_window > 0)
    setenv("NEEDLE_KV_WINDOW", String::num_int64(kv_window).utf8().get_data(),
           1);
  if (threads > 0)
    setenv("NEEDLE_THREADS", String::num_int64(threads).utf8().get_data(), 1);

  // Find the original library
  String lib_name = _get_platform_library_name();
  Vector<String> search_paths;

  // Try multiple search paths
  String ext_dir = OS::get_singleton()->get_executable_path().get_base_dir();
  search_paths.push_back(ext_dir.path_join(lib_name));
  search_paths.push_back(library_path);
  search_paths.push_back(_get_cache_dir().path_join(lib_name));
  search_paths.push_back(
      String("res://addons/needle_for_godot/bin/").path_join(lib_name));
  search_paths.push_back(
      String("res://addons/needle_for_godot/bin/linux/").path_join(lib_name));

  String original_path;
  for (const String &path : search_paths) {
    if (path.is_empty()) {
      continue;
    }
    String fs_path = path;
    if (path.begins_with("res://")) {
      fs_path = ProjectSettings::get_singleton()->globalize_path(path);
    }
    if (FileAccess::file_exists(fs_path)) {
      original_path = fs_path;
      break;
    }
  }

  if (original_path.is_empty()) {
    ERR_PRINT("NeedleAgent: Could not find needle library. Ensure "
              "libneedle.so/dll is accessible.");
    return;
  }

  // Copy the library to a unique temp path to get isolated state
  // Each dlopen of a different inode gets its own data/bss segments
#ifdef _WIN32
  char tmp_dir[MAX_PATH];
  char tmp_file[MAX_PATH];
  GetTempPathA(MAX_PATH, tmp_dir);
  GetTempFileNameA(tmp_dir, "ndl", 0, tmp_file);
  lib_copy_path = String(tmp_file);
  // On Windows, GetTempFileName creates the file; we need to copy manually
  DeleteFileA(tmp_file);
  // Use copyfile approach
  CopyFileA(original_path.utf8().get_data(), lib_copy_path.utf8().get_data(),
            FALSE);
#else
  char tmp_template[] = "/tmp/needle_agent_XXXXXX";
  int tmp_fd = mkstemp(tmp_template);
  if (tmp_fd < 0) {
    ERR_PRINT("NeedleAgent: Failed to create temp file for library copy");
    return;
  }

  // Copy the original .so to the temp path
  FILE *src = fopen(original_path.utf8().get_data(), "rb");
  if (!src) {
    close(tmp_fd);
    unlink(tmp_template);
    ERR_PRINT("NeedleAgent: Failed to open original library for copying");
    return;
  }

  char copy_buf[65536];
  size_t n;
  while ((n = fread(copy_buf, 1, sizeof(copy_buf), src)) > 0) {
    ssize_t written = write(tmp_fd, copy_buf, n);
    (void)written;
  }
  fclose(src);
  close(tmp_fd);

  lib_copy_path = String(tmp_template);
#endif

  // Load the copy — this gets its own isolated state
  lib_handle = dlopen_compat(lib_copy_path.utf8().get_data(), RTLD_LAZY);
  if (lib_handle == nullptr) {
    ERR_PRINT("NeedleAgent: Failed to load library copy: " + String(dlerror()));
    unlink(lib_copy_path.utf8().get_data());
    lib_copy_path = "";
    return;
  }

  fn_load = (needle_load_fn)dlsym_compat(lib_handle, "needle_load");
  fn_init = (needle_init_fn)dlsym_compat(lib_handle, "needle_init");
  fn_complete = (needle_complete_fn)dlsym_compat(lib_handle, "needle_complete");
  fn_reset = (needle_reset_fn)dlsym_compat(lib_handle, "needle_reset");

  if (!fn_load || !fn_init || !fn_complete || !fn_reset) {
    ERR_PRINT("NeedleAgent: Library found but missing required symbols");
    dlclose_compat(lib_handle);
    lib_handle = nullptr;
    fn_load = nullptr;
    fn_init = nullptr;
    fn_complete = nullptr;
    fn_reset = nullptr;
    unlink(lib_copy_path.utf8().get_data());
    lib_copy_path = "";
    return;
  }
}

void NeedleAgent::_unload_library() {
  if (lib_handle != nullptr) {
    dlclose_compat(lib_handle);
    lib_handle = nullptr;
    fn_load = nullptr;
    fn_init = nullptr;
    fn_complete = nullptr;
    fn_reset = nullptr;
  }

  if (!lib_copy_path.is_empty()) {
    unlink(lib_copy_path.utf8().get_data());
    lib_copy_path = "";
  }
}

// --- Model loading ---

void NeedleAgent::_load_model_from_file(const String &p_path) {
  Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
  if (f.is_null()) {
    emit_signal("failed", String("Could not open model file: ") + p_path);
    return;
  }

  int64_t file_size = f->get_length();
  PackedByteArray data = f->get_buffer(file_size);
  f->close();

  bool load_ok = false;
  String load_error;

  {
    std::lock_guard<std::mutex> lock(api_mutex);
    int rc = fn_load(data.ptr(), file_size);

    // Free the model buffer immediately — needle_load copies internally
    data = PackedByteArray();
    data.resize(0);

    if (rc < 0) {
      load_error =
          String("needle_load failed with code: ") + String::num_int64(rc);
    } else {
      model_loaded = true;

      // If tools are set, initialize immediately
      String init_err;
      if (!_try_init_needle(init_err)) {
        load_error = init_err;
      }
      load_ok = true;
    }
  } // api_mutex released here

  // Emit signals outside the lock to avoid deadlock
  if (!load_error.is_empty()) {
    emit_signal("failed", load_error);
    return;
  }

  emit_signal("loaded");
}

// --- Public methods ---

void NeedleAgent::load_model(const String &p_path) {
  if (p_path.is_empty()) {
    ERR_PRINT("NeedleAgent: load_model requires a non-empty path");
    return;
  }

  model_path = p_path;

  if (!model_loaded) {
    _copy_and_load_library();
    if (lib_handle == nullptr) {
      emit_signal("failed", "Needle library not found");
      return;
    }
  }

  _load_model_from_file(p_path);
}

Dictionary NeedleAgent::complete(const String &p_text) {
  Dictionary result;

  if (!model_loaded || !init_done) {
    result["type"] = "error";
    result["error"] = "Model not loaded. Call load_model() first.";
    return result;
  }

  if (tools_json.is_empty()) {
    result["type"] = "error";
    result["error"] = "No tools defined. Call set_tools() first.";
    return result;
  }

  // Reuse the class-level output buffer (no allocation per call)
  std::memset(output_buffer, 0, OUTPUT_BUFFER_SIZE);

  std::lock_guard<std::mutex> lock(api_mutex);
  int rc = fn_complete(p_text.utf8().get_data(), max_new_tokens, output_buffer,
                       OUTPUT_BUFFER_SIZE);

  if (rc < 0) {
    result["type"] = "error";
    result["error"] =
        String("needle_complete failed with code: ") + String::num_int64(rc);
    return result;
  }

  // Parse JSON response
  String response_str = String::utf8(output_buffer);
  Ref<JSON> json;
  json.instantiate();
  Error parse_err = json->parse(response_str);
  if (parse_err != OK) {
    result["type"] = "error";
    result["error"] = "Failed to parse needle response: " + response_str;
    return result;
  }

  result = json->get_data();
  return result;
}

void NeedleAgent::reset() {
  std::lock_guard<std::mutex> lock(api_mutex);
  if (fn_reset) {
    fn_reset();
  }
  init_done = false;

  // Re-initialize if tools are set
  if (model_loaded && !tools_json.is_empty()) {
    String init_error;
    _try_init_needle(init_error);
  }
}

bool NeedleAgent::is_model_loaded() const { return model_loaded; }
