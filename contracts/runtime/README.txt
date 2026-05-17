P1 (*contracts/runtime*) — roadmap **P1-1** … **P1-7** (execution context, address
space, preemption, PMM, domain arenas, driver reentrancy, timekeeping).

Add headers here as they are authored. Each new header should start with:
  #include "contract_extend.h"
so **P0 foundations** stay the base (see ../foundations/).

When this tree is used, add **-Icontracts/runtime** to the relevant Makefile or
CMake target (same pattern as **-Icontracts/foundations**).
