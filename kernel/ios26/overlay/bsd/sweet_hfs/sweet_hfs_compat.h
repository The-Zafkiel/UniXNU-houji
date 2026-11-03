/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Build HFS using its KPI view. Keep the baseline HFSX features explicitly;
 * the iPhoneOS compiler target must not select AppleKeyStore protection.
 */
#ifndef HOUJI26_HFS_COMPAT_H
#define HOUJI26_HFS_COMPAT_H
#undef BSD_KERNEL_PRIVATE
#undef XNU_KERNEL_PRIVATE
#undef CONFIG_PROTECT
#undef CONFIG_SEARCHFS
#undef CONFIG_HFS_DIRLINK
#undef CONFIG_SECLUDED_RENAME
#undef CONFIG_MACF
#undef CONFIG_HFS_RBTREE
#undef QUOTA
#undef NAMEDSTREAMS
#undef FIFO
#undef HFS_COMPRESSION
#include <TargetConditionals.h>
/* Use the same HFS feature/KPI view as the baseline macOS HFS sources. */
#undef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 0
#undef TARGET_OS_IOS
#define TARGET_OS_IOS 0
#include "core/kext-config.h"
#undef CONFIG_PROTECT
#define CONFIG_PROTECT 0
#undef CONFIG_SECLUDED_RENAME
#define CONFIG_SECLUDED_RENAME 0
#undef QUOTA
#define QUOTA 1
#define NAMEDSTREAMS 1
#define CONFIG_HFS_DIRLINK 1
#define CONFIG_SEARCHFS 1
#define OSKextGetCurrentLoadTag sweet_hfs_current_load_tag
#endif
