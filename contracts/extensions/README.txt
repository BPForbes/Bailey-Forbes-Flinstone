Phase-specific contract trees (preferred layout):
  P1 → ../runtime/     (see README there)
  P2 → ../identity/
  P3 → ../networking/  (umbrella: contract_networking.h; see README there)
  P4 → ../drivers/     (umbrella: contract_drivers.h; see README there)
  P5 → ../storage/   (umbrella: contract_storage.h; see README there)

This *extensions* directory is for cross-cutting optional helpers or small shared
pieces that do not belong to a single phase tree. Each header here should still
start with: #include "contract_extend.h"
(that file lives in ../foundations/). Add -Icontracts/extensions when something
here is first compiled in.

Compile-time optional helpers: define FL_CONTRACT_HAS_<Name> from the build or
from a small extension header when you add optional .c helpers; gate extended
behaviour with #if defined(FL_CONTRACT_HAS_<Name>). See contract_compile_ext.h
for the master FL_CONTRACT_COMPILE_EXTENSIONS switch and conventions.

P0-3 through P0-8 normative contract headers are contract_p0_ci.h,
contract_p0_arm_gic.h, contract_p0_x86_idt.h, contract_p0_x86_gdt.h,
contract_p0_fdt.h, and contract_p0_uart.h under ../foundations/ (included from
contract_foundations.h).
