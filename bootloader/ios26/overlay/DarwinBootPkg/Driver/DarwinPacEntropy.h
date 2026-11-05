/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DARWIN_PAC_ENTROPY_H
#define DARWIN_PAC_ENTROPY_H
#define DARWIN_PAC_ENTROPY_BYTES 80U

/* Five independently generated 128-bit keys. A trial image may provision the
 * complete 80 bytes from the host CSPRNG when firmware has no entropy provider.
 * This is deliberately not a timer PRNG or an expansion of the kernel seed.
 * Such images must be generated afresh and consumed once by the host launcher.
 */
typedef BOOLEAN (*DARWIN_PAC_ENTROPY_READER)(VOID *Cookie, UINT8 *Keys);

STATIC VOID DarwinPacErase(VOID *Buffer, UINTN Bytes) {
  volatile UINT8 *Out = (volatile UINT8 *)Buffer;
  while (Bytes--) *Out++ = 0;
}

STATIC BOOLEAN DarwinPacEntropyValid(CONST UINT8 *Keys) {
  for (UINTN Key = 0; Key < 5; ++Key) {
    UINT8 Nonzero = 0;
    for (UINTN Byte = 0; Byte < 16; ++Byte) Nonzero |= Keys[Key * 16 + Byte];
    if (Nonzero == 0) return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN DarwinPacSelectEntropy(UINT8 *Keys,
    DARWIN_PAC_ENTROPY_READER Hardware, VOID *Cookie,
    UINT8 *Trial, UINTN TrialBytes, BOOLEAN *UsedTrial) {
  *UsedTrial = FALSE;
  DarwinPacErase(Keys, DARWIN_PAC_ENTROPY_BYTES);
  if ((Trial == NULL && TrialBytes != 0) ||
      (Trial != NULL && TrialBytes != DARWIN_PAC_ENTROPY_BYTES)) return FALSE;
  BOOLEAN Ready = Hardware(Cookie, Keys) && DarwinPacEntropyValid(Keys);
  if (!Ready) {
    DarwinPacErase(Keys, DARWIN_PAC_ENTROPY_BYTES);
    if (Trial != NULL && DarwinPacEntropyValid(Trial)) {
      CopyMem(Keys, Trial, DARWIN_PAC_ENTROPY_BYTES);
      *UsedTrial = TRUE;
      Ready = TRUE;
    }
  }
  /* Consume even unused trial material, including the hardware success case. */
  if (Trial != NULL) DarwinPacErase(Trial, DARWIN_PAC_ENTROPY_BYTES);
  return Ready;
}
#endif
