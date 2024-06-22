
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include "logs.h"

FILE* log_fh;
pthread_mutex_t log_mutex;

void init_logging() {
    log_fh = fopen("archivist.log", "w");
    if (log_fh==NULL) {
        fprintf(stderr, "Failed to open log file archivist.log\n");
        exit(1);
    }

    if (pthread_mutex_init(&log_mutex, NULL)!=0) {
        fprintf(stderr, "Failed to create a mutex for the log file\n");
        exit(1);
    }

    setvbuf(log_fh, NULL, _IOLBF, 0);
}

int log_status(const char* context, int rc, const char* format, ...) {
    va_list ap;
    time_t now;
    struct tm local;

    time(&now);
    localtime_r(&now, &local);

    pthread_mutex_lock(&log_mutex);

    fprintf(log_fh, "%02d:%02d:%02d : %-6s : %-10s : (%-5d) : ", local.tm_hour, local.tm_min, local.tm_sec, "STATUS", context, rc);

    va_start(ap, format);
    vfprintf(log_fh, format, ap);
    va_end(ap);

    fprintf(log_fh, "\n");

    pthread_mutex_unlock(&log_mutex);

    return rc;
}

void log_info(const char* context, const char* format, ...) {
    va_list ap;
    time_t now;
    struct tm local;

    time(&now);
    localtime_r(&now, &local);

    pthread_mutex_lock(&log_mutex);

    fprintf(log_fh, "%02d:%02d:%02d : %-6s : %-10s : ", local.tm_hour, local.tm_min, local.tm_sec, "INFO", context);

    va_start(ap, format);
    vfprintf(log_fh, format, ap);
    va_end(ap);

    fprintf(log_fh, "\n");

    pthread_mutex_unlock(&log_mutex);
}

int log_error(const char* context, int err_no, const char* format, ...) {
    va_list ap;
    time_t now;
    struct tm local;

    time(&now);
    localtime_r(&now, &local);

    pthread_mutex_lock(&log_mutex);

    fprintf(log_fh, "%02d:%02d:%02d : %-6s : %-10s : (%-5d) : %-20s : ", local.tm_hour, local.tm_min, local.tm_sec, "ERROR", context, err_no , (err_no!=0?strerror(err_no):""));

    va_start(ap, format);
    vfprintf(log_fh, format, ap);
    va_end(ap);

    fprintf(log_fh, "\n");

    pthread_mutex_unlock(&log_mutex);

    return -err_no;
}
