#ifndef FL_ELEVATION_H
#define FL_ELEVATION_H

#include "contract_p2_elevation.h"
#include "contract_result.h"
#include <stdint.h>

fl_result_t fl_elevation_grant(const char *principal, const char *reason, fl_elevation_token_t *out);
int         fl_elevation_active(fl_elevation_token_t token);
void        fl_elevation_revoke(fl_elevation_token_t token);
void        fl_elevation_revoke_all(void);

#endif /* FL_ELEVATION_H */
