# Test fixtures and seams

Recorded from source comments removed in PR #416 (2026-11).

This page lists the shared fixtures under `tests/` and the link-time seams some tests use. It is a reference for
writing a new test, not a description of what each test checks.

## Rules

- A secret is never a literal in a test. Passwords, PINs and secret bytes are drawn at run time (`fl_random_bytes` or
  `kq_test_random_hex`). `0xAA` is used only as a wipe sentinel, to show that a buffer was overwritten.
- A test message never prints a secret, and never prints the result of a call that carries one. It names the command
  line instead.
- Helpers that build a fixture are `static inline` in a header, so each test binary gets its own copy and no test
  links against another test's objects.

## Shared helpers: `tests/kq_test_util.h`

The base header has no dependencies beyond the platform headers. Two macros add more helpers:
`KQ_TEST_UTIL_SQLITE` adds the SQLite helpers, and `KQ_TEST_UTIL_KQ` adds the KeyQuorum helpers.

| Helper | Purpose |
|--------|---------|
| `kq_test_rmrf(path)` | removes a tree; it never follows a symbolic link; 0, or -1 if anything is left |
| `kq_test_write_file(path, data, n)` | writes bytes to a new file |
| `kq_test_write_text(path, text)` | writes a string to a new file |
| `kq_test_random_hex(out, nbytes)` | random bytes as hex, at most 64 bytes; the raw bytes are wiped before it returns |
| `kq_test_open_db(path, &db)` | opens a SQLite file (`KQ_TEST_UTIL_SQLITE`) |
| `kq_test_exec_sql(path, sql)` | runs one statement on a SQLite file (`KQ_TEST_UTIL_SQLITE`) |
| `kq_test_pair_account(...)` | writes an `account_keys` row for a label, with its device id and public keys (`KQ_TEST_UTIL_SQLITE`) |
| `kq_test_put_container(path, c)` | writes a `.kqtf` container beside `path`, atomically (`KQ_TEST_UTIL_KQ`) |
| `kq_test_fork(drv, one, author, key, t_left, t_right, t_merge, out)` | makes a forked history from one revision, with two check-ins and a merge (`KQ_TEST_UTIL_KQ`) |

## Link-time seams

Some tests replace a function at link time with `-Wl,--wrap=<name>`. The Makefile lists each one.

| Symbol | Test target | What the wrapper does |
|--------|-------------|-----------------------|
| `fl_kq_driver_get_or_register` | `test_heritage`, `test_kq_real_token` | returns a copy of the real driver that the test's patch function edits, so callers can be checked against a changed driver |
| `asm_mem_secure_zero` | `test_secret_compare` | records the pointer, length and count of each secure wipe, then calls the real function, so the test can check what was wiped and last |
| `cmd_sudo_sign_elevated` | `test_interpreter_sudo_gate` | counts its calls, so the test can check how often the gate runs |

A wrapper calls `__real_<name>` for the original.

## Where the helpers are used

`test_kq_checkpoint.c`, `test_kq_delivery.c`, `test_kq_net.c`, `test_kq_service.c` and `test_kq_real_token.c` use
the KeyQuorum helpers. `test_kq_checkpoint.c`, `test_kq_service.c`, `test_heritage.c`, `test_drivers.c` and
`test_kq_real_token.c` use the account-pairing and container helpers.

For the storage table these fixtures exercise, see [KQ_STORAGE.md](KQ_STORAGE.md#tests-that-pin-this). For the lab's
browser tests, see [LAB_KQ_SHELL.md](LAB_KQ_SHELL.md#tests).
