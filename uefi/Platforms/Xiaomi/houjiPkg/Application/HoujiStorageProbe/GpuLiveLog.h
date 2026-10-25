/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Pre-CP canary only: allow one application-TPL file snapshot under an explicit
 * live-GMU/empty-queues/no-CP memory-isolation owner. Always restore worker TPL.
 */
#ifndef HOUJI_GPU_LIVE_LOG_H
#define HOUJI_GPU_LIVE_LOG_H
#include <stdint.h>
enum {HLL_OK,HLL_ARGUMENT,HLL_TPL,HLL_GUARD,HLL_SAVE,HLL_RESTORE};
struct hll_result {unsigned code,phase,lowered,restored,guards,before_tpl,after_tpl;uint64_t status;};
struct hll_io {void *cookie;unsigned (*tpl)(void *);void (*lower)(void *);int (*restore)(void *);int (*guard)(void *);uint64_t (*save)(void *);};
static inline unsigned hll_run(const struct hll_io *io,struct hll_result *r) {
 if(!r)return HLL_ARGUMENT;*r=(struct hll_result){0};
 if(!io || !io->tpl || !io->lower || !io->restore || !io->guard || !io->save)return r->code=HLL_ARGUMENT;
 r->phase=1;r->before_tpl=io->tpl(io->cookie);if(r->before_tpl!=16)return r->code=HLL_TPL;
 r->guards++;if(io->guard(io->cookie))return r->code=HLL_GUARD;
 r->phase=2;io->lower(io->cookie);r->lowered=1;
 if(io->tpl(io->cookie)!=4)r->code=HLL_TPL;
 else {r->guards++;if(io->guard(io->cookie))r->code=HLL_GUARD;
  else {r->phase=3;r->status=io->save(io->cookie);if(r->status>>63)r->code=HLL_SAVE;}}
 r->phase=4;int restored=io->restore(io->cookie);r->after_tpl=io->tpl(io->cookie);
 if(restored || r->after_tpl!=16)return r->code=HLL_RESTORE;r->restored=1;
 r->guards++;if(io->guard(io->cookie))r->code=HLL_GUARD;
 r->phase=5;return r->code;
}
#endif
