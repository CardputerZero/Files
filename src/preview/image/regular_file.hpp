#pragma once

#include <cstdio>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

namespace files {

// Decoder entry points are also used by tests and small utilities, so they
// cannot rely only on PreviewSupport's path check.  Opening with O_NONBLOCK
// and verifying the descriptor prevents a FIFO or device node from blocking
// the caller if it is passed directly.
inline std::FILE* openRegularFile(const std::string& path)
{
#if defined(__unix__) || defined(__APPLE__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return nullptr;
    }

    struct stat stat_buffer {};
    if (::fstat(fd, &stat_buffer) != 0 || !S_ISREG(stat_buffer.st_mode)) {
        ::close(fd);
        errno = EINVAL;
        return nullptr;
    }

    std::FILE* file = ::fdopen(fd, "rb");
    if (!file) {
        const int saved_errno = errno;
        ::close(fd);
        errno = saved_errno;
    }
    return file;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

}  // namespace files
