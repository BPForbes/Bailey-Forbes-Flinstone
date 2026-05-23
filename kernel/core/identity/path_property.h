#ifndef FL_PATH_PROPERTY_H
#define FL_PATH_PROPERTY_H

#include "contract_p2_principal.h"
#include "contract_p2_authz.h"

/* Lab cap (32) matches FL_USER_NAME_MAX; contract allows up to FL_CREDENTIAL_USERNAME_MAX_CHARS. */
#define FL_PATH_OWNER_MAX 32u
#define FL_PATH_META_DIR  ".flmeta"
#define FL_PATH_META_FILE ".flmeta/properties.json"

typedef struct fl_path_property {
    char owner[FL_PATH_OWNER_MAX];
    int  requires_elevation;
    int  valid;
} fl_path_property_t;

/** Resolve effective properties by walking from path upward (directories preferred). */
fl_result_t fl_path_property_resolve(const char *path, fl_path_property_t *out);

/** Write properties for a directory (creates .flmeta/ under dir_path). */
fl_result_t fl_path_property_set_dir(const char *dir_path, const char *owner, int requires_elevation);

#endif /* FL_PATH_PROPERTY_H */
