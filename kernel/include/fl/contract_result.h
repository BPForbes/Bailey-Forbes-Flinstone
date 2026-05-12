/**
 * Shared fallible-result vocabulary (**P0-2**).
 *
 * New driver, VFS, and net entry points should prefer `fl_result_t` over raw
 * `int` magic where the call can fail for reasons callers must handle.
 * Reserve `0` for success; use negative values for errors (errno-shaped or
 * project-specific). Document **who frees** any out-parameters on every API.
 */
#ifndef FL_CONTRACT_RESULT_H
#define FL_CONTRACT_RESULT_H

typedef int fl_result_t;

#define FL_RESULT_OK 0

/** Unspecified failure; prefer a more specific negative code when defined. */
#define FL_RESULT_ERR (-1)

/** Invalid argument / precondition violation. */
#define FL_RESULT_INVAL (-22)

/** Operation not supported in this build or configuration. */
#define FL_RESULT_NOSYS (-38)

#endif /* FL_CONTRACT_RESULT_H */
