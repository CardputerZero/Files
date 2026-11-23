#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <unistd.h>
static int slowed;
static void slow(int fd)
{
    char relative[32], path[4096];
    snprintf(relative, sizeof relative, "/proc/self/fd/%d", fd);
    ssize_t count = readlink(relative, path, sizeof(path) - 1);
    if (count <= 0) return;
    path[count] = 0;
    if (!strstr(path, ".files-transfer-")) return;
    const char *after = getenv("FILES_COPY_SLOW_AFTER");
    if (after && lseek(fd, 0, SEEK_CUR) < atoll(after)) return;
    if (__atomic_exchange_n(&slowed, 1, __ATOMIC_SEQ_CST)) return;
    FILE *marker = fopen(getenv("FILES_COPY_MARKER"), "w");
    if (marker) fclose(marker);
    sleep(3);
}
ssize_t sendfile(int out, int in, off_t *offset, size_t count)
{
    ssize_t (*next)(int, int, off_t *, size_t) = dlsym(RTLD_NEXT, "sendfile");
    slow(out);
    return next(out, in, offset, count);
}
ssize_t write(int fd, const void *data, size_t count)
{
    ssize_t (*next)(int, const void *, size_t) = dlsym(RTLD_NEXT, "write");
    if (count >= 65536) slow(fd);
    return next(fd, data, count);
}
