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

/**
 * **Driver probe** (`fl_driver_ops_t::probe`): return **`FL_RESULT_OK`** when this
 * driver claims the device and **`attach`** may run. Return **`FL_RESULT_NOSYS`**
 * when the device is not handled (the registry tries other drivers). Other
 * negative **`fl_result_t`** values mean a definitive probe failure.
 */
#define FL_RESULT_PROBE_SKIP FL_RESULT_NOSYS

/**
 * Inclusive bounds for the JSON **`rc`** field in FL1 packed history rows (**P0-2**).
 * Values outside this range are rejected by **fl_history_record_unpack_cmd** so random
 * JSON integers cannot be mistaken for vetted **fl_result_t** codes.
 */
#define FL_RESULT_JSON_RC_MIN (-99999)
#define FL_RESULT_JSON_RC_MAX (99999)

_Static_assert(FL_RESULT_JSON_RC_MIN < FL_RESULT_JSON_RC_MAX,
               "FL_RESULT_JSON_RC inclusive bounds");

#endif /* FL_CONTRACT_RESULT_H */
