Future P1–P9 (Px) contract headers should live in this directory when added.
Each Px header should start with: #include "contract_extend.h"
(that file lives in ../foundations/ and pulls the full P0 base). Add
-Icontracts/extensions to the build when the first Px header is introduced.
