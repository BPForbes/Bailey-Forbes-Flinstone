P1 memory modules (hosted + bare-metal paths):
  fl_stack.c     — fixed-capacity uintptr stack (PMM free-frame pool)
  exec_context.c — P1-1 execution context (heap + stack + IP + GPR bank)
  ../mm/pmm.c    — P1-4 physical frame allocator (uses fl_stack for free frames)
