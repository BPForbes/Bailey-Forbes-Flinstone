/**
 * **P5-5 — Server share staging** (module contract, normative).
 *
 * **Distribution:** when a remote peer offers a file and the recipient has no file at
 * the captured path, the default landing directory is **`server_share/`** at the
 * recipient's session root (relative to the logged-in user's cwd unless policy
 * redirects). See **docs/SERVER.md**.
 */
#ifndef FL_CONTRACT_P5_SERVER_SHARE_H
#define FL_CONTRACT_P5_SERVER_SHARE_H

#include "contract_extend.h"

#define FL_CONTRACT_P5_5_SERVER_SHARE_CONTRACT_DEFINED 1

/** Default directory name (single path component; no trailing slash in the constant). */
#define FL_SERVER_SHARE_DIR_NAME "server_share"

/** Max bytes for a normalized relative path under the share tree (including NUL budget). */
#ifndef FL_SERVER_SHARE_REL_PATH_MAX
#define FL_SERVER_SHARE_REL_PATH_MAX 4096u
#endif

/** Max bytes stored for the sender's captured absolute/resolved path on the wire. */
#ifndef FL_SERVER_SHARE_SENDER_PATH_MAX
#define FL_SERVER_SHARE_SENDER_PATH_MAX FL_SERVER_SHARE_REL_PATH_MAX
#endif

_Static_assert(FL_SERVER_SHARE_REL_PATH_MAX >= 256u,
               "server_share path cap must cover normal user trees");

#endif /* FL_CONTRACT_P5_SERVER_SHARE_H */
