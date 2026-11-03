#ifndef FL_WQ_JOURNAL_SQLITE_H
#define FL_WQ_JOURNAL_SQLITE_H

/*
 * Hosted, flushed backend for the workqueue journal (FL-25): one table in a SQLite file, WAL with
 * synchronous = FULL, so an append returns only after the commit is on disk. kernel/core/sched must not
 * depend on the KeyQuorum driver (the scheduler is below the security driver), so this file knows only the
 * ops table. The hash-chained, event-sourced form (KeyQuorum JobStateChanged) is a later step: this is the
 * plain table, and the file's location is the caller's choice.
 */

#include "workqueue.h"

/** Open (creating if needed) the journal at path and fill *ops. FL_RESULT_ERR on failure. */
fl_result_t fl_wq_journal_sqlite_open(const char *path, fl_wq_journal_ops_t *ops);
/** Close what open returned (the ops table is unusable afterwards). Not while work is running. */
void fl_wq_journal_sqlite_close(fl_wq_journal_ops_t *ops);

#endif
