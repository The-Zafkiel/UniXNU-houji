// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only Android helper: bypass block-page cache on the one fixed 128 MiB chunk of the 12 GiB userdata prefix.
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    // An index is the only input; callers cannot select another path or window.
    if (argc != 2 || !argv[1][0] || strlen(argv[1]) > 2) return 64;
    unsigned index = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '9') return 64;
        index = index * 10 + (unsigned)(*p - '0');
    }
    if (index >= 96) return 64;
    char path[4096];
    if (!realpath("/dev/block/by-name/userdata", path) || strcmp(path, "/dev/block/sda33")) return 2;
    int fd = open(path, O_RDONLY | O_DIRECT | O_CLOEXEC);
    struct stat st;
    uint64_t size = 0;
    int sector = 0;
    if (fd < 0 || fstat(fd, &st) || !S_ISBLK(st.st_mode) ||
        ioctl(fd, BLKGETSIZE64, &size) || size != 515465261056ULL ||
        ioctl(fd, BLKSSZGET, &sector) || sector != 4096) return 3;
    void *buffer = NULL;
    if (posix_memalign(&buffer, 4096, 131072)) return 4;
    for (unsigned block = 0; block < 1024; block++) {
        if (pread(fd, buffer, 131072, ((off_t)index * 1024 + block) * 131072) != 131072) return 5;
        size_t done = 0;
        while (done < 131072) {
            ssize_t n = write(STDOUT_FILENO, (char *)buffer + done, 131072 - done);
            if (n <= 0) return 6;
            done += (size_t)n;
        }
    }
    free(buffer);
    return close(fd) ? 7 : 0;
}
