/**
 * **P5 — Server file permission bits** (module contract, normative).
 *
 * Native Flinstone server-file sharing uses a 16-bit permission and state word.
 * SFTP is only a compatibility adapter over these bits; it must not bypass
 * revocation, expiration, receiver disposition, or server-share policy.
 */
#ifndef FL_CONTRACT_P5_FILE_PERMS_H
#define FL_CONTRACT_P5_FILE_PERMS_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P5_FILE_PERMS_CONTRACT_DEFINED 1

/** Preferred long-term permission word for server file offers. */
typedef uint16_t fl_file_perms_t;

#define FL_FILE_PERM_OVERWRITE      0x0001u /* bit 0  */
#define FL_FILE_PERM_WRITE          0x0002u /* bit 1  */
#define FL_FILE_PERM_EDIT           0x0004u /* bit 2  */
#define FL_FILE_PERM_RUN            0x0008u /* bit 3  */
#define FL_FILE_PERM_RESERVED_LOW   0x0010u /* bit 4  */
#define FL_FILE_PERM_CREATE         0x0020u /* bit 5  */
#define FL_FILE_FLAG_SCAN_REQUIRED  0x0040u /* bit 6  */
#define FL_FILE_FLAG_SFTP_RESERVED  0x0080u /* bit 7  */
#define FL_FILE_PERM_VIEW           0x0100u /* bit 8  */
#define FL_FILE_PERM_DOWNLOAD       0x0200u /* bit 9  */
#define FL_FILE_PERM_SERVER_SHARE   0x0400u /* bit 10 */
#define FL_FILE_PERM_ACCEPT         0x0800u /* bit 11 */
#define FL_FILE_FLAG_LOCKED         0x1000u /* bit 12 */
#define FL_FILE_FLAG_EXPIRES        0x2000u /* bit 13 */
#define FL_FILE_FLAG_PUBLIC         0x4000u /* bit 14 */
#define FL_FILE_FLAG_REVOKED        0x8000u /* bit 15 */

/**
 * Compact v1 fallback layout if an implementation must serialize one byte.
 *
 * This is not a low-byte truncation of fl_file_perms_t: compact bit
 * positions intentionally differ from the 16-bit native permission word
 * (for example ACCEPT is bit 4 here, but bit 11 in fl_file_perms_t).
 * Use fl_file_perms8_to_16() when widening compact wire data.
 */
typedef uint8_t fl_file_perms8_t;

#define FL_FILE8_PERM_OVERWRITE  0x01u
#define FL_FILE8_PERM_WRITE      0x02u
#define FL_FILE8_PERM_EDIT       0x04u
#define FL_FILE8_PERM_RUN        0x08u
#define FL_FILE8_PERM_ACCEPT     0x10u
#define FL_FILE8_FLAG_EXPIRES    0x20u
#define FL_FILE8_FLAG_PUBLIC     0x40u
#define FL_FILE8_FLAG_REVOKED    0x80u

static inline fl_file_perms_t fl_file_perms8_to_16(fl_file_perms8_t compact)
{
    fl_file_perms_t perms = 0u;

    if (compact & FL_FILE8_PERM_OVERWRITE)
        perms |= FL_FILE_PERM_OVERWRITE;
    if (compact & FL_FILE8_PERM_WRITE)
        perms |= FL_FILE_PERM_WRITE;
    if (compact & FL_FILE8_PERM_EDIT)
        perms |= FL_FILE_PERM_EDIT;
    if (compact & FL_FILE8_PERM_RUN)
        perms |= FL_FILE_PERM_RUN;
    if (compact & FL_FILE8_PERM_ACCEPT)
        perms |= FL_FILE_PERM_ACCEPT;
    if (compact & FL_FILE8_FLAG_EXPIRES)
        perms |= FL_FILE_FLAG_EXPIRES;
    if (compact & FL_FILE8_FLAG_PUBLIC)
        perms |= FL_FILE_FLAG_PUBLIC;
    if (compact & FL_FILE8_FLAG_REVOKED)
        perms |= FL_FILE_FLAG_REVOKED;

    return perms;
}

static inline fl_file_perms_t fl_file_perms_normalize(fl_file_perms_t perms)
{
    if (perms & FL_FILE_PERM_EDIT) {
        perms |= FL_FILE_PERM_VIEW;
    }

    if (perms & FL_FILE_PERM_RUN) {
        perms |= FL_FILE_PERM_VIEW;
    }

    if (perms & FL_FILE_PERM_WRITE) {
        perms |= FL_FILE_PERM_VIEW;
    }

    return perms;
}

static inline int fl_file_perms_has(fl_file_perms_t perms,
                                    fl_file_perms_t required)
{
    return (perms & required) == required;
}

static inline int fl_file_perms_is_revoked(fl_file_perms_t perms)
{
    return (perms & FL_FILE_FLAG_REVOKED) != 0;
}

/**
 * Revoke check for the session layer's big-endian MSB scan.
 *
 * Only bit 15 is a denial signal: the packet path reads the most significant
 * permission bit first and blocks dispatch when FL_FILE_FLAG_REVOKED is set.
 * Other high-byte flags are metadata and must not deny by this helper. The
 * shift form mirrors the session layer's MSB-first permission scan.
 */
static inline int fl_file_msb_denied(fl_file_perms_t perms)
{
    return (perms >> 15) != 0;
}

static inline int fl_file_perms_is_public(fl_file_perms_t perms)
{
    return (perms & FL_FILE_FLAG_PUBLIC) != 0;
}

static inline int fl_file_perms_can_overwrite(fl_file_perms_t perms)
{
    return (perms & FL_FILE_PERM_OVERWRITE) != 0;
}

static inline int fl_file_perms_can_view(fl_file_perms_t perms)
{
    return (fl_file_perms_normalize(perms) & FL_FILE_PERM_VIEW) != 0;
}

static inline int fl_file_perms_can_edit(fl_file_perms_t perms)
{
    return (perms & FL_FILE_PERM_EDIT) != 0;
}

static inline int fl_file_perms_can_run(fl_file_perms_t perms)
{
    return (perms & FL_FILE_PERM_RUN) != 0;
}

static inline int fl_file_perms_can_write(fl_file_perms_t perms)
{
    return (perms & FL_FILE_PERM_WRITE) != 0;
}

static inline int fl_file_share_is_expired(uint64_t expires_at, uint64_t now)
{
    return expires_at != 0u && now > expires_at;
}

static inline fl_result_t fl_file_share_validate_access(fl_file_perms_t file_perms,
                                                        uint64_t expires_at,
                                                        uint64_t now)
{
    if (file_perms & FL_FILE_FLAG_REVOKED) {
        return FL_RESULT_ACCES;
    }

    if ((file_perms & FL_FILE_FLAG_EXPIRES) &&
        expires_at != 0u &&
        now > expires_at) {
        return FL_RESULT_ACCES;
    }

    return FL_RESULT_OK;
}

#endif /* FL_CONTRACT_P5_FILE_PERMS_H */
