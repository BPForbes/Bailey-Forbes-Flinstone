# Formal models (Lean 4)

`formal/lean/` holds a small Lean 4 model of the durable workqueue (FL-25) and of two rules the scheduler and the
session depend on. The proofs are about the model. Two functions of it are turned into C and compiled into the
kernel, so the C that decides which journal state changes are allowed is the model's, not a second copy.

| File | What it is |
|---|---|
| `lean/FlWqCode.lean` | Two `UInt8 -> UInt8` functions: `fl_wq_transition_allowed` and `fl_wq_state_terminal`. Plain scalar code, so Lean's C for them needs no Lean runtime. |
| `lean/FlWq.lean` | The model and its theorems, including that `FlWqCode` is the model (`code_allowed_matches`, `code_terminal_matches`, `code_refuses_non_states`). |
| `lean/extract_c.py` | Keeps only the two runtime-free functions from Lean's C output (not its boxed wrappers or initializer) and records the sha256 of `FlWqCode.lean`. Refuses a function that uses Lean objects. |
| `lean/build.sh` | Checks every theorem, then regenerates `kernel/core/sched/wq_model_gen.inc`. Needs Lean: `nix shell nixpkgs#lean4 -c formal/lean/build.sh` (Lean 4.35.0 was used). |
| `lean/ref_check.c` | A readable hand translation of `FlWqCode.lean` into C (same if chain, same literals, a comment per branch naming the model arm). A check only: it compares itself with the generated C over all 65,536 (from, to) pairs and 256 states. Run by `make check-formal`. |
| `lean/check_generated.sh` | Needs no Lean: the committed `.inc` must come from the current `FlWqCode.lean` (`make check-formal`, run in CI). |

`ref_check.c` is there so a person can read the C equivalent of the Lean next to it. It is never linked into the
kernel or any test, so there is still one implementation in the kernel: the generated one.

## What is proven (in the model)

- **State machine.** The twelve moves the queue makes and no others; a terminal state (DONE, FAILED, EXPIRED,
  SKIPPED) never moves; no state moves to itself; RUNNING is reached only from QUEUED (so a handler runs only after
  the RUNNING write), DONE only from RUNNING, HELD only from PENDING.
- **Attempts.** With the retry rule of `workqueue.c`, no reachable job makes more than `max` attempts, and a job
  that is waiting (PENDING, QUEUED, HELD) always has an attempt left.
- **Staging bounds.** Reserve, publish and release keep: durable jobs in the window plus reservations at most
  half the cap; ordinary plus durable plus reserved items at most `pending max - protected reserve`. An ordinary
  submit that counts reservations cannot take a slot a claim has promised.
- **Session key.** A key derived for one login is published only while that login's generation is current: after
  a logout, a late publish is refused and changes nothing.

All theorems depend only on Lean's three standard axioms (`propext`, `Classical.choice`, `Quot.sound`), no `sorry`.

## How the C is tied to it

- `kernel/core/sched/wq_model_gen.inc` is included into `workqueue.c`. Every journal state change the queue asks
  for goes through `wq_mark`, which refuses (and counts, `fl_wq_illegal_moves`) a move the model forbids. The
  memory journal's terminal check is the generated one.
- `tests/test_workqueue_p18.c` checks all 65,536 (from, to) pairs against a list of the twelve moves written in
  the test, not derived from the generated code, and that the counter stayed 0 after every durable scenario.
  `tests/test_wq_journal_sqlite.c` checks the same counter for the flushed journal.
- Changing `FlWqCode.lean` so it no longer matches the model fails the Lean build (checked: removing
  QUEUED -> PENDING makes `code_allowed_matches` fail).

## What this does not do

- It does **not** prove `workqueue.c` correct. The attempt, staging and session theorems are about the model; the
  C is tied to them by the guard and by tests, not by a proof of refinement. Locking, threads and memory are not
  modelled.
- Lean's general C output uses Lean's runtime (reference-counted objects) and is not usable in the kernel, which
  has none. Only functions written on scalars, with their wrappers cut off, can be taken; `extract_c.py` enforces
  that. Larger Lean programs would need that runtime or a different extraction.
- CI does not run Lean (it would add a toolchain download); it checks only that the generated file matches its
  source. Run `build.sh` after any change to the model.
