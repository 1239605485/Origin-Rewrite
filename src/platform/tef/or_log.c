#include "or_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
#include <android/log.h>
#endif

/* The loader-provided logger is useful but not sufficient: some TEFManager
 * exports omit the module stream, and a malformed logger argument can abort
 * the process while converting text.  Always format once into UTF-8-safe
 * ASCII-oriented text, then fan out to the loader, files and logcat. */
static FILE *g_private_log;
static FILE *g_export_log;
static size_t g_private_bytes;
static size_t g_export_bytes;
static bool g_private_capped;
static bool g_export_capped;
static size_t g_log_calls;

#define OR_LOG_FILE_LIMIT (8u * 1024u * 1024u)
#define OR_LOG_MESSAGE_LIMIT 32768u

static int android_priority(mod_log_level_t level) {
#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
    switch (level) {
        case MOD_LOG_LEVEL_TRACE:
        case MOD_LOG_LEVEL_DEBUG: return ANDROID_LOG_DEBUG;
        case MOD_LOG_LEVEL_WARNING: return ANDROID_LOG_WARN;
        case MOD_LOG_LEVEL_ERROR: return ANDROID_LOG_ERROR;
        case MOD_LOG_LEVEL_CRITICAL:
        case MOD_LOG_LEVEL_FATAL: return ANDROID_LOG_FATAL;
        case MOD_LOG_LEVEL_INFO:
        default: return ANDROID_LOG_INFO;
    }
#else
    (void)level;
    return 0;
#endif
}

static void prepare_log(FILE **file, size_t *bytes, bool *capped) {
    long end;
    if (!file || !*file || !bytes || !capped) return;
    if (fseek(*file, 0L, SEEK_END) != 0) return;
    end = ftell(*file);
    if (end < 0) return;
    if ((size_t)end > OR_LOG_FILE_LIMIT) {
        FILE *replacement = freopen(NULL, "w", *file);
        if (replacement) {
            *file = replacement;
            end = 0L;
        }
    }
    *bytes = (size_t)end;
    *capped = *bytes >= OR_LOG_FILE_LIMIT;
}

static void write_file(FILE *file, size_t *bytes, bool *capped,
                       mod_log_level_t level, const char *message) {
    int written;
    if (!file || !bytes || !capped || !message) return;
    if (*capped) {
        if (level != MOD_LOG_LEVEL_FATAL && level != MOD_LOG_LEVEL_CRITICAL) return;
        return;
    }
    written = fprintf(file, "[%lld] %s\n", (long long)time(NULL), message);
    if (written > 0) *bytes += (size_t)written;
    if (*bytes >= OR_LOG_FILE_LIMIT) *capped = true;
    (void)fflush(file);
}

void or_log_init(const kernel_mod_handle_t *handle) {
    char private_path[512];
    static const char export_path[] =
        "/storage/emulated/0/Android/data/eternal.future.tefmanager/"
        "files/logs/tefkernel/runtime_originrewrite.log";

    or_log_shutdown();
    g_private_bytes = 0u;
    g_export_bytes = 0u;
    g_private_capped = false;
    g_export_capped = false;
    g_log_calls = 0u;
    private_path[0] = '\0';
    if (handle && handle->private_dir) {
        (void)snprintf(private_path, sizeof(private_path), "%s/%s",
                       handle->private_dir, "originrewrite_runtime.log");
        /* Start a bounded per-load session; an old runaway log must not be
         * carried into the next launch. */
        g_private_log = fopen(private_path, "w");
        prepare_log(&g_private_log, &g_private_bytes, &g_private_capped);
    }
    g_export_log = fopen(export_path, "w");
    prepare_log(&g_export_log, &g_export_bytes, &g_export_capped);
}

void or_log_shutdown(void) {
    if (g_private_log) {
        fclose(g_private_log);
        g_private_log = NULL;
    }
    if (g_export_log) {
        fclose(g_export_log);
        g_export_log = NULL;
    }
}

void or_log_write(mod_log_level_t level, const char *fmt, ...) {
    char message[1024];
    va_list args;
    if (!fmt) return;

    va_start(args, fmt);
    (void)vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    /* Protect the loader/logcat bridge as well as the files.  A restart loop
     * must not retain hundreds of thousands of formatted messages in a host
     * log collector.  Fatal/critical records still pass through. */
    if (g_log_calls >= OR_LOG_MESSAGE_LIMIT &&
        level != MOD_LOG_LEVEL_FATAL && level != MOD_LOG_LEVEL_CRITICAL) return;
    if (g_log_calls < OR_LOG_MESSAGE_LIMIT) ++g_log_calls;

    /* Pass one plain %s argument to the loader. This avoids forwarding a
     * variadic format with pointers or size_t values into its text bridge. */
    if (mod_logger_write) {
        mod_logger_write(level, "OriginRewrite", "%s", message);
    }
    write_file(g_private_log, &g_private_bytes, &g_private_capped, level, message);
    write_file(g_export_log, &g_export_bytes, &g_export_capped, level, message);
#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
    __android_log_print(android_priority(level), "OriginRewrite", "%s", message);
#else
    (void)android_priority(level);
    (void)fprintf(stderr, "[OriginRewrite] %s\n", message);
#endif
}
