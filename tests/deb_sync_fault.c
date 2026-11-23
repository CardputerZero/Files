#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* sync_point(int fd)
{
    char link[64];
    char path[PATH_MAX];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    ssize_t size = readlink(link, path, sizeof(path) - 1);
    if (size < 0) return NULL;
    path[size] = '\0';
    if (strcmp(path, "/var/tmp") == 0) return "parent";
    const char prefix[] = "/var/tmp/cardputer-files-install-";
    if (strncmp(path, prefix, sizeof(prefix) - 1) != 0) return NULL;
    const char* leaf = strchr(path + sizeof(prefix) - 1, '/');
    if (!leaf) return "stage";
    if (strcmp(leaf, "/package.deb") == 0) return "archive";
    if (strcmp(leaf, "/pending-package.json.tmp") == 0) return "record";
    return NULL;
}

int fsync(int fd)
{
    int (*real_fsync)(int) = dlsym(RTLD_NEXT, "fsync");
    const char* trace = getenv("TEST_SYNC_TRACE");
    const char* point = trace ? sync_point(fd) : NULL;
    if (!point) return real_fsync(fd);

    const char* fault = getenv("TEST_SYNC_FAIL");
    const int inject = fault && strcmp(fault, point) == 0;
    const int result = inject ? -1 : real_fsync(fd);
    const int error = inject ? EIO : errno;
    FILE* log = fopen(trace, "a");
    if (!log) abort();
    fprintf(log, "%s%s\n", result == 0 ? "" : "failed ", point);
    if (fclose(log) != 0) abort();
    errno = error;
    return result;
}
