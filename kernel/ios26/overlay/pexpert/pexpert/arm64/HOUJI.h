/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef _PEXPERT_ARM64_HOUJI26_H_
#define _PEXPERT_ARM64_HOUJI26_H_
/* Keep the qualified houji CPU granule and native driver source guards.
 * BCM2837 supplies generic ARM timer/PSCI/GIC and standard PAC, not MMIO
 * addresses or vendor firmware; those remain in the houji handoff/ADT.
 */
#define __ARM_16K_PG__ 1
#define HOUJI_IOS26 1
#define SWEET 1
#include <pexpert/arm64/BCM2837.h>
#endif
