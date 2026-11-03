/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * A static arm64e PID1 for the first kernel/ABI check, without dyld or libc.
 * The kernel's bounded recovery timer ends the session. This is not a GUI.
 */
static long call3(long number, long a, long b, long c) {
    register long selector __asm__("x16") = number;
    register long result __asm__("x0") = a;
    register long second __asm__("x1") = b;
    register long third __asm__("x2") = c;
    __asm__ volatile("svc #0x80" : "+r"(result) : "r"(selector), "r"(second), "r"(third) : "cc", "memory");
    return result;
}
void houji26_main(void);
__asm__(".text\n.globl _start\n.p2align 2\n_start:\nmov x9, sp\nand sp, x9, #~15\nb _houji26_main\n");
void houji26_main(void) {
    static const char message[] = "\nHOUJI IOS26 ARM64E PID1 REACHED\nNative kernel and user entry; GPU/GUI not started in this probe.\n";
    long console = call3(5, (long)"/dev/console", 2, 0);
    call3(4, console, (long)message, sizeof(message) - 1);
    for (;;) __asm__ volatile("yield");
}
