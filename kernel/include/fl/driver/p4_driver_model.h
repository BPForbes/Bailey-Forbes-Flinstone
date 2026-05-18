/**
 * **P4-1** driver model v2 verification hooks.
 */
#ifndef FL_P4_DRIVER_MODEL_H
#define FL_P4_DRIVER_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/** Returns 0 when **s_model_lock** guards static driver tables (lab self-test). */
int fl_driver_model_self_test_s_model_lock(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_DRIVER_MODEL_H */
