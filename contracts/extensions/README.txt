Future P1–P9 (Px) contract headers should live in this directory when added.
Each Px header should start with: #include "contract_extend.h"
(that file lives in ../foundations/ and pulls the full P0 base). Add
-Icontracts/extensions to the build when the first Px header is introduced.

Compile-time optional helpers: define FL_CONTRACT_HAS_<Name> from the build or
from a small extension header when you add optional .c helpers; gate extended
behaviour with #if defined(FL_CONTRACT_HAS_<Name>). See contract_compile_ext.h
for the master FL_CONTRACT_COMPILE_EXTENSIONS switch and conventions.
