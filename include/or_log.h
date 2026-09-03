#ifndef OR_LOG_H
#define OR_LOG_H

#include "mod_core.h"
#include "mod_logger.h"

void or_log_init(const kernel_mod_handle_t *handle);
void or_log_shutdown(void);
void or_log_write(mod_log_level_t level, const char *fmt, ...);

#endif
