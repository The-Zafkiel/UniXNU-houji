/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * An explicit RAM bring-up profile may reset after a returned boot attempt.
 * The host prearms Recovery; this code never reads or writes a partition.
 */
#ifndef UNIXNU_RETURN_POLICY_H
#define UNIXNU_RETURN_POLICY_H
STATIC inline BOOLEAN
UniXNUParseReturnPolicy(CONST CHAR8 *Action, INT64 Seconds, BOOLEAN *Reset, UINTN *Delay)
{
  *Reset = FALSE;
  *Delay = 0;
  if (Action == NULL || Seconds < 1 || Seconds > 30) return FALSE;
  if (AsciiStrCmp(Action, "menu") != 0 && AsciiStrCmp(Action, "reset") != 0) return FALSE;
  *Reset = AsciiStrCmp(Action, "reset") == 0;
  *Delay = (UINTN)Seconds;
  return TRUE;
}
STATIC inline EFI_STATUS
UniXNUResetAfterReturn(UINTN Seconds, EFI_STATUS Status,
    VOID (*Draw)(VOID *Context, EFI_STATUS Status, UINTN Seconds), VOID *Context)
{
  if (Seconds < 1 || Seconds > 30 || Draw == NULL) return EFI_INVALID_PARAMETER;
  for (; Seconds != 0; --Seconds) {
    Draw(Context, Status, Seconds);
    gBS->Stall(1000000);
  }
  gRT->ResetSystem(EfiResetCold, Status, 0, NULL);
  return EFI_DEVICE_ERROR; /* A returning ResetSystem never authorizes a retry. */
}
#endif
