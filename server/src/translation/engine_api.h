#pragma once

// Versioned C ABI between the Server and the optional, separately distributed CPU engine.
// No STL objects, ownership of heap memory, exceptions or user text cross into a logger.
#include <stdint.h>

// Published last by the verified component installer; inference must ignore partial extraction.
#define YT_TRANSLATE_COMPONENT_RECEIPT ".installed"

#ifdef _WIN32
#define YT_TRANSLATE_CALL __cdecl
#ifdef YT_TRANSLATE_ENGINE_EXPORTS
#define YT_TRANSLATE_EXPORT __declspec(dllexport)
#else
#define YT_TRANSLATE_EXPORT
#endif
#else
#define YT_TRANSLATE_CALL
#define YT_TRANSLATE_EXPORT
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    enum YtTranslateStatus
    {
        YT_TRANSLATE_OK = 0,
        YT_TRANSLATE_UNAVAILABLE = 1,
        YT_TRANSLATE_CANCELLED = 2,
        YT_TRANSLATE_TIMEOUT = 3,
        YT_TRANSLATE_ERROR = 4,
        YT_TRANSLATE_BUFFER_TOO_SMALL = 5,
        YT_TRANSLATE_INVALID_ARGUMENT = 6
    };

    typedef int32_t(YT_TRANSLATE_CALL *YtTranslateCancelled)(void *context);

    typedef struct YtTranslateApi
    {
        uint32_t struct_size;
        uint32_t version;
        // All paths/text are UTF-8, lengths exclude the terminating NUL. Call on a background thread.
        int32_t(YT_TRANSLATE_CALL *create)(const char *directory, void **engine);
        void(YT_TRANSLATE_CALL *destroy)(void *engine);
        // Output belongs to caller, and is NUL-terminated on success. A timed-out or cancelled
        // partial hypothesis must never be committed. Deadline includes tokenization and encoding.
        int32_t(YT_TRANSLATE_CALL *translate)(void *engine, const char *source, uint32_t source_bytes,
                                              uint32_t timeout_ms, YtTranslateCancelled cancelled, void *context,
                                              char *output, uint32_t output_capacity, uint32_t *output_bytes);
    } YtTranslateApi;

    typedef int32_t(YT_TRANSLATE_CALL *YtTranslateGetApi)(uint32_t version, YtTranslateApi *api);
    YT_TRANSLATE_EXPORT int32_t YT_TRANSLATE_CALL yt_translate_get_api(uint32_t version, YtTranslateApi *api);

#ifdef __cplusplus
}
#endif
