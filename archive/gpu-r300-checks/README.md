# Historical r300 implementation checks

These fixtures extract helper functions that were removed when replacing the
219f013 runtime and the old Metal frontend with the matching development stack.
They are retained as reference and are not tests of the current renderer.

- Old allocation retry policy: replaced by the current runtime allocator and its
  memory-type reclaim path. Current host coverage is `gpu-memory-cache.py`,
  `gpu-room-worker.py`, and the matching runtime memory unit tests.
- Green fresh-texture probe: removed, including its build option; authored pixel
  checks use `tools/check-metal-pixels.py` against the live frontend instead.
- Old idle-primer policy: replaced by the matched upstream primer. Priming is
  disabled by default on houji and needs an explicit diagnostic boot argument.

No previous test result qualifies the replacement stack on hardware.

旧 CPU sampler、旧前端阶段导出器与旧采样策略头文件也保留在这里。它们不被当前
插件编译，相关 sampler fixture 不再作为当前实现的检查。当前阶段 ring 检查执行
所选运行时源码；CPU sampler 默认关闭。
