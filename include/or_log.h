#ifndef OR_LOG_H
#define OR_LOG_H
void or_log_open(const char *private_dir);
void or_log_close(void);
void or_log(const char *tag, const char *format, ...);
#endif
