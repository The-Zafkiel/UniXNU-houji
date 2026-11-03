/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_IOACCELERATOR26_H
#define HOUJI_IOACCELERATOR26_H
#include <IOKit/IOService.h>

/* One real metaclass shared by the iOS26 framebuffer and A750 service. Its
 * implementation remains in MiniIOMobileFramebuffer.cpp; do not emit a second
 * iOS16 definition with a different virtual-method list into the same image.
 */
class IOAcceleratorES : public IOService {
    OSDeclareDefaultStructors(IOAcceleratorES);
public:
    IOReturn setProperties(OSObject *) APPLE_KEXT_OVERRIDE;
};
#endif
