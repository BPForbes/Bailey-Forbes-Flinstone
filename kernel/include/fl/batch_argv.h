/**
 * Batch-mode argv grouping for **P7** regression tests (Codex): `contracts` must not
 * consume a following `audit` token as a sub-argument.
 */
#ifndef FL_BATCH_ARGV_H
#define FL_BATCH_ARGV_H

#ifdef __cplusplus
extern "C" {
#endif

int fl_batch_contracts_tokens_count(int argc, char **argv, int i);
int fl_batch_audit_tokens_count(int argc, char **argv, int i);

#ifdef __cplusplus
}
#endif

#endif /* FL_BATCH_ARGV_H */
