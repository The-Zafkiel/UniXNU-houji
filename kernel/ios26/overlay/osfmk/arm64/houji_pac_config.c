/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <arm64/proc_reg.h>
#include <stdint.h>

/* The loader reads the compiled kernel's TTBR1 PAC geometry rather than
 * guessing it from another board or an old disassembly. No keys live here.
 */
__attribute__((used, section("__DATA,__pac_config")))
const uint64_t houji_pac_config[4] = {
    0x3147464341505548ULL, /* HUPACFG1 */
    (0x3fULL << 16) | (1ULL << 38) | (1ULL << 52),
    TCR_EL1_BOOT & ((0x3fULL << 16) | (1ULL << 38) | (1ULL << 52)),
    80
};
