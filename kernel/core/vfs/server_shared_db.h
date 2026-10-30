#ifndef SERVER_SHARED_DB_H
#define SERVER_SHARED_DB_H

#include "contract_p5_server_catalog.h"

#include <sqlite3.h>

/* Internal to the catalog's two users, server_shared_db.c (the share path) and kq_delivery.c
 * (the KeyQuorum delivery store); server_catalog_file.c implements it. */

/* The kinds the share path serves; a NULL kind (only in a crafted file) is not one of them. */
#define FL_CATALOG_SHAREABLE_KINDS_SQL_C "COALESCE(payload_kind,0) IN (1,2)"

/* Creates the file owner-only if missing (an existing one must be a regular file with one name,
 * never a link), opens it, waits FL_SERVER_CATALOG_BUSY_TIMEOUT_MS on a busy file, and applies
 * the schema and version stamp; a file from a newer build is refused. A file we cannot write is
 * opened read-only and left as it is. */
fl_result_t fl_catalog_file_open(const char *db_path, sqlite3 **out);
fl_result_t fl_catalog_file_user_version(sqlite3 *sql, int *out);

/* One event in a transfer's history (kq = 0: the share path, which may write before its row
 * exists and never for a row of another kind; kq = 1: only for an existing KQ row).
 * FL_RESULT_ACCES and nothing written when the row is not of that family. */
fl_result_t fl_catalog_file_append_event(sqlite3 *sql, int kq, const char *transfer_id,
                                         const char *event, uint16_t actor_member_id,
                                         uint64_t at, const char *detail);
/* That family's events for one id, oldest first; none for a row of the other family. */
fl_result_t fl_catalog_file_list_events(sqlite3 *sql, int kq, const char *transfer_id,
                                        fl_server_catalog_event_t *out, size_t out_cap,
                                        size_t *out_count);

#endif /* SERVER_SHARED_DB_H */
