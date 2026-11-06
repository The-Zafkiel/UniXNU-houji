/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_ROOT_FS26_H
#define HOUJI_ROOT_FS26_H
/* The native device is a qualified HFSX volume. Mockfs accepts any device
 * and would expose that whole volume as its single executable. */
static inline int hjrfs26_candidate(int native_root,const char *name) {
 return native_root!=1 || (name && name[0]=='h' && name[1]=='f' && name[2]=='s' && name[3]==0);
}
#endif
