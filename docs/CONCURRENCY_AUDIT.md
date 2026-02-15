# Concurrency Correctness Audit

## Thread Pool Ordering

- **PQ invariant**: Workers `pq_pop` by priority (lowest first); FIFO tie-break.
- **Job completion**: Each `submit_single_command_priority` blocks on `job->done`; caller waits for command to finish before returning.
- **Ordering**: Commands complete in PQ order. No out-of-order completion for serial submissions.

## Undo Stack

- **Access**: `push_undo` (on create/save/move), `fm_undo`, `fm_undo_available`.
- **Protection**: `undo_mutex` guards undo_stack; required when multiple threads can invoke FM ops.
- **Serial default**: Normal shell flow submits one command and blocks; BATCH_SINGLE_THREAD runs inline. Undo mutex ensures correctness if concurrency is introduced.

## File Ops

- **Provider**: Local provider uses host `fopen`/`fread`/`fwrite`; not inherently thread-safe.
- **Isolation**: Each command runs to completion; no interleaved reads/writes from the same command.
- **Disk writes**: `update_cluster_line` uses atomic rename (write to .tmp, rename); safe against concurrent reads of main file.
