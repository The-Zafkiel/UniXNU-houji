/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * An inconclusive cache precondition cannot enable a coherent fast path.
 * Keep the previously qualified owner, with explicit cache maintenance.
 */
static bool houjiCoherenceFallback26(unsigned status) {
 auto &p=gHoujiCoherence;auto &a=gHoujiApi;
 if(status!=11 || gHoujiLive.mode!=5 || native_adreno_io_coherent ||
    !p.active || p.phase!=3 || p.code || p.attempts!=HoujiCoherenceAttempts || p.dirty>=48 ||
    p.copies || p.draws || p.pass || p.render_pass || p.unmapped ||
    !p.handle || !p.command_mapped || a.phase!=12 || !houjiCoherenceCpuViews() ||
    !houjiVmIdle() || a.submit.outstanding() || !houjiFullLease() || !houjiFullTables())return false;
 // No coherence command was submitted. Remove only this probe's two
 // mappings while their descriptors, CPU views and original owner stay pinned.
 p.code=a.vm.unmap(p.handle);if(p.code || houjiApiAuditTables())return false;
 p.handle=0;
 p.code=a.client.unmapRetainedForTrial(a.handle[2]);
 if(p.code || !houjiApiBo(2,0) || houjiApiAuditTables())return false;
 p.command_mapped=0;p.unmapped=1;
 p.code=NativeAdrenoMaintainOwnedPages(p.backing.mapping(),HoujiCoherenceBytes,NativeAdrenoCleanInvalidate);
 if(p.code || !houjiVmIdle() || a.submit.outstanding() || !houjiFullLease() || !houjiFullTables())return false;
 native_adreno_io_coherent=NativeAdrenoCoherenceOff;
 a.phase=10;p.active=0;gHoujiLive.mode=3;
 return true;
}
