#ifndef NEEDLE_H
#define NEEDLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Needle C API - reconstructed from Python ctypes bindings (_worker.py)
 *
 * The API owns one process-global conversation.
 * All status functions use negative values for failure.
 * needle_load returns 0 on success.
 * needle_init returns the number of cached prefix tokens (>=0 on success).
 * needle_complete returns the number of generated tokens (>=0 on success).
 */

/* Load the model from raw bytes. Returns 0 on success, negative on error. */
int needle_load(const void *model_bytes, uint64_t model_size);

/* Initialize a session with system prompt and tool declarations.
 * tool_index may be NULL. Returns prefix token count (>=0) or negative on error. */
int needle_init(const char *system, const char *tools_json, const char *tool_index);

/* Complete a query. Writes JSON response into output buffer.
 * Returns token count (>=0) or negative on error. */
int needle_complete(const char *text, int max_new_tokens, char *output, int output_size);

/* Reset the conversation, keeping tools loaded. */
void needle_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* NEEDLE_H */
