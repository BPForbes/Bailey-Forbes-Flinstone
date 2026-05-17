Future P1–P9 (Px) contract headers should live in this directory when added.
Each Px header should start with: #include "contract_extend.h"
(that file lives in ../foundations/ and pulls the full P0 base). Add
-Icontracts/extensions to the build when the first Px header is introduced.

Compile-time optional helpers: define FL_CONTRACT_HAS_<Name> from the build or
from a small extension header when you add optional .c helpers; gate extended
behaviour with #if defined(FL_CONTRACT_HAS_<Name>). See contract_compile_ext.h
for the master FL_CONTRACT_COMPILE_EXTENSIONS switch and conventions.

P0-3 through P0-8 normative contract headers are contract_p0_ci.h,
contract_p0_arm_gic.h, contract_p0_x86_idt.h, contract_p0_x86_gdt.h,
contract_p0_fdt.h, and contract_p0_uart.h under ../foundations/ (included from
contract_foundations.h).
