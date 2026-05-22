# PR #154 — CodeRabbit inline comment resolutions

Audit date: 2026-05-22 (branch `cursor/nasm-abi-issue-reports-7cb1`).

| CR topic | Verdict | Notes |
|----------|---------|-------|
| **ARM-ABI-002** kernel “legacy style” doc | **Fixed** | `docs/issues/arm/ARM-ABI-002-*.md` status **Closed**; table already **Synced**. |
| **ARM-SYNC-001** present-tense `ARM/` | **Already fixed** | Doc **Closed**; historical vs current wording. |
| **kernel `alloc_core.S` `.hidden` BSS** | **Already fixed** | `heap_end`, `free_head`, `alloc_lock` after `.comm` in canonical + kernel. |
| **kernel `alloc_free.S` 104-byte frame** | **Already fixed** | `#-80` / `#80` in canonical + kernel (AAPCS64). |
| **kernel NASM `heap_end`/`free_head` `:hidden`** | **Already fixed** | Issue **#155**; `global heap_end:hidden`. |
| **Makefile kernel GAS override** | **Already fixed** | Comment at `kernel/arch/x86_64/%.o` rule (lines 398–402). |
| **`version_def.h` BUILD line** | **Skipped** | Change is from **GitHub Actions** version flow, not hand-edited agent output (@BPForbes confirmed). |
| **Issues #159–#163** | **Fixed** | Commit `b40d336`. |
| **Issues #155–#158** | **Fixed** | Earlier commits on this branch. |
| **#156 realloc `rep movsb`** | **Fixed** | `asm_mem_copy` in NASM/GAS/kernel. |

No further asm changes required for the inline threads above as of this pass.
