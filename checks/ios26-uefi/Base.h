/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Scalar-only host view of the UEFI ABI; no firmware implementation. */
#ifndef HOUJI26_HOST_BASE_H
#define HOUJI26_HOST_BASE_H
#include <stddef.h>
#include <stdint.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef char CHAR8;
#define STATIC_ASSERT _Static_assert
#define OFFSET_OF offsetof
#endif
