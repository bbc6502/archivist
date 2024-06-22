#ifndef __LOGS__
#define __LOGS__

extern void init_logging();
extern int log_status(const char* context, int rc, const char* format, ...);
extern void log_info(const char* context, const char* format, ...);
extern int log_error(const char* context, int err_no, const char* format, ...);

#endif
