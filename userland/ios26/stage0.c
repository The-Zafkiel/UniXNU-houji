/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * The HFS root already contains /redwood; no donor partition or md1 is mounted.
 */
#include "mini.h"
int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv;
    int fd = open("/dev/console", O_RDWR, 0);
    if (fd >= 0) {
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        if (fd > 2) close(fd);
    }
    char *arguments[] = {"/redwood/sbin/init", NULL};
    execve(arguments[0], arguments, envp);
    printf("HOUJI26: cannot execute userland init: %s\n", strerror(errno));
    reboot(0x08);
    return 1;
}
