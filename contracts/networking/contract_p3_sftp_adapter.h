/**
 * **P3/P5 — SFTP compatibility adapter contract** (module contract, normative).
 *
 * SFTP-like operations map onto the native Flinstone server-file contract. The
 * adapter must not bypass file permissions, revocation, expiration, nickname /
 * member routing, audit metadata, receiver disposition, or `/server_share` policy.
 */
#ifndef FL_CONTRACT_P3_SFTP_ADAPTER_H
#define FL_CONTRACT_P3_SFTP_ADAPTER_H

#include "contract_p3_file_session.h"

#define FL_CONTRACT_P3_SFTP_ADAPTER_CONTRACT_DEFINED 1

#define FL_SFTP_VPATH_HOME         "/sftp/home"
#define FL_SFTP_VPATH_INBOX        "/sftp/share/inbox"
#define FL_SFTP_VPATH_PUBLIC       "/sftp/share/public"
#define FL_SFTP_VPATH_SENT         "/sftp/share/sent"
#define FL_SFTP_VPATH_SERVER_SHARE "/sftp/server_share"

fl_result_t fl_sftp_adapter_open(const char *virtual_path,
                                 fl_file_perms_t requested_perms);

fl_result_t fl_sftp_adapter_read(const char *virtual_path,
                                 uint64_t offset,
                                 uint8_t *out,
                                 uint32_t out_cap,
                                 uint32_t *out_len);

fl_result_t fl_sftp_adapter_write(const char *virtual_path,
                                  uint64_t offset,
                                  const uint8_t *data,
                                  uint32_t data_len);

fl_result_t fl_sftp_adapter_list(const char *virtual_path);

fl_result_t fl_sftp_adapter_remove_or_revoke(const char *virtual_path);

#endif /* FL_CONTRACT_P3_SFTP_ADAPTER_H */
