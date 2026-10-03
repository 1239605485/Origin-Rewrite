#include "or_log.h"
#include "mod_logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
__attribute__((visibility("default"))) void (*mod_logger_write)(mod_log_level_t,const char *,const char *,...)=NULL;
static FILE *file;
static unsigned lines;
void or_log_open(const char *dir) {
    or_log_close(); lines=0;
    if (!dir || !*dir) return;
    char path[1024]; int n=snprintf(path,sizeof(path),"%s/originrewrite_rebuild.log",dir);
    if (n>0 && (size_t)n<sizeof(path)) file=fopen(path,"w");
}
void or_log_close(void) { if (file) fclose(file); file=NULL; }
void or_log(const char *tag, const char *format, ...) {
    if (lines++>=3000) return;
    char message[768]; va_list args; va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    if (mod_logger_write) mod_logger_write(MOD_LOG_LEVEL_INFO,tag,"%s",message);
    if (file) { fprintf(file,"[%s] %s\n",tag,message); fflush(file); }
}
