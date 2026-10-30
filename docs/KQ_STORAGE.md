# KeyQuorum checkpoint storage

How the KeyQuorum checkpoints (`kernel/core/vfs/kq_checkpoint.c`) read and write files, and why.
Issue #384, D14. For the delivery flow itself see [FILE_SHARING_KEYQUORUM.md](FILE_SHARING_KEYQUORUM.md).

## The idea

KeyQuorum's logic modules never touch the disk. They take a `Storage` (`src/storage.rs` in
KeyQuorum), and `NativeStorage` carries the placement rules: a new file never replaces another,
a rename never replaces, and so on. The checkpoints here work the same way. They decide what a
delivery means (readiness, freshness, the supervisor's permit) and ask a **storage table**
(`kq_storage.h`, `fl_kq_storage_t`) for every read and write.

## The call path

```
cmd_server_file.c, lab_kq.c, tests
   |  fl_kq_cp_encode / decode / decode_in / history / head / reseal / permit
   v
kq_checkpoint.c   decides what a delivery means; calls the KeyQuorum driver
   |  fl_kq_cp_storage()  ->  the table in force
   |
   +-- default table (in kq_checkpoint.c) ----------------------------+
   |                                                                    v
   +-- facade table (kq_storage_facade.c)                       server_shared_fs.c
          |  fm_read_bytes, fm_write_atomic, fm_dir_*           (fl_fs_*: staged writes,
          v                                                      no-follow reads, the
       fs_facade.c   policy, write chain, events, path log       directory-relative landing,
          |  fs_provider_dir_*  (the provider's contained slots)  fl_fs_commit_files)
          v                                                            ^
       fs_provider.c  local provider  -------------------------------+
```

The default table calls the placement layer directly. The facade table goes through the facade and
the provider, which are built on the same placement layer, so no path is ever what a landing is
written through.

## The table

Two kinds of access, because the sender's files and the recipient's landing differ:

- **By path.** The sender's file and the container beside it (`encode`, `history`, `head`,
  `reseal`, `permit`).
- **By held directory.** The recipient's landing (`decode`). The directory is opened once
  (`fl_kq_dir_t`: an fd and its path) and every name is used inside it, so a link planted at a name
  is never followed, and the payload, container and seen record replace together or not at all.

| Operation | Meaning | Default table | Facade table |
|-----------|---------|---------------|--------------|
| `read_file(path, max)` | bounded read | `fl_fs_read_file` | `fm_read_bytes` (policy `can_read`, path log) |
| `write_atomic(path)` | full write through a sibling, then rename | `fl_fs_write_file_atomic` | `fm_write_atomic` (write chain, save event, path log) |
| `exists(path)` | 1 present, 0 absent, -1 cannot tell | `access`; only `ENOENT`/`ENOTDIR` mean absent | the default's (the facade has no existence call) |
| `dir_open` / `dir_close` | hold a landing directory | `fl_fs_dir_open` | `fm_dir_open` (policy `can_read` on the directory) |
| `dir_read(name, max)` | bounded read of one name | `fl_fs_read_file_at` | `fm_dir_read` |
| `dir_exists(name)` | existence of one name (a link counts) | `fl_fs_exists_at` | `fm_dir_exists` |
| `dir_read_tail(name, unit, keep)` | the newest `keep` records of a file of whole `unit`-byte records; missing is empty, not whole records is -1 | `fl_fs_read_tail_at` | `fm_dir_read_tail` |
| `dir_commit(files, n)` | replace 1 to 4 files, all or none | `fl_fs_commit_files` | `fm_dir_commit` (write chain for **every** file before any is staged; events after success) |

Results mean the same in every table: `0`, `-1`, `-2` larger than the bound, `-3` could not be
opened; data comes back as `MEM_DOMAIN_FS` memory.

## What a decode does

1. Hold the landing directory (`dir_open`), or use the one the caller passes to
   `fl_kq_cp_decode_in` (the caller keeps ownership and the checkpoint never opens it again by path).
2. Refuse a name that is already there and not tracked (`dir_exists` on the leaf and its `.kqseen`).
3. Open and verify the letter with the KeyQuorum driver; load the seen record (`dir_read_tail`,
   64-byte pairs, newest 64); check freshness and the local container (`dir_read`).
4. Commit the payload, the `.kqtf` container and the `.kqseen` record together (`dir_commit`).
   A refusal anywhere before this installs nothing; a failed commit puts back what it replaced.

`server file open` hands the decode the directory the destination resolver
(`fl_server_shared_resolve_local_path`) already holds, so a directory swapped after that check is
not followed.

## Who installs which table

| Caller | Table |
|--------|-------|
| `userland/command/cmd_server_file.c` (`kq_use_storage`, once per `server file` command) | facade, when the shell's file manager service exists |
| the browser lab (`lab_kq.c`) and the unit tests | default |

`fl_kq_cp_set_storage(table)` installs a table (NULL restores the default) and returns the one it
replaced. The table is process-wide, like the KeyQuorum driver; set it at start, not while a
checkpoint runs.

## Rules

- A table never changes what a result means, and never joins a name to a path where the contained
  calls exist: a directory that cannot be held is refused.
- A policy refusal on any file of a commit stops the whole commit before anything is staged.
- Names inside a held directory are plain: no separator, not `.` or `..`.
- New file-moving code asks the placement layer or this table. It does not open paths itself.

## Adding a backend

1. Fill an `fl_kq_storage_t` (every slot) over your storage and keep it alive for as long as it is
   installed.
2. Pass the existing results contract above. Return `-1` rather than guessing when you cannot tell.
3. Install it with `fl_kq_cp_set_storage`. Test it by running `tests/test_kq_checkpoint.c` against it:
   the counting table there shows which calls a delivery makes.
4. If it is for the lab, it must not pull the facade or the native session code into the wasm build.

## Tests that pin this

- `tests/test_kq_checkpoint.c`: `test_checkpoints_use_the_storage_table` (every call goes through the
  installed table), `test_a_refused_commit_installs_nothing`, `test_an_unreadable_container_is_not_replaced`,
  `test_decode_uses_the_held_directory` (a directory swapped after it was held), and
  `test_checkpoints_through_the_facade` (a policy refusal stages nothing; events follow success).
- `tests/test_fs_provider_bytes.c`: the provider's contained slots, a link at a name, the ENOSYS
  answers of an older provider, and the facade's policy and event behaviour.

## Known limits

- `exists` is not routed through the facade.
- The sender-side calls work by path, not by a held directory: they act on the user's own file and the
  container beside it.
- Other file code (the delivery store, the catalog, the lab's reader) uses the placement layer directly
  and is not behind this table.
- The browser lab uses the default table; the facade table is native only.
- The restore-failure path of `fl_fs_commit_files` has no test that forces a rename to fail.
