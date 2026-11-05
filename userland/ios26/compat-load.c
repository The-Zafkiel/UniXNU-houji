/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <dlfcn.h>
/* Loaded by the version-matched injection library. The compatibility library
 * uses runtime class/selector checks and handles later framework loads itself.
 */
__attribute__((constructor)) static void houji26_load_compatibility(void) {
    (void)dlopen("/redwood/lib/HoujiKeyboardGeometry.dylib", RTLD_NOW | RTLD_LOCAL);
}
