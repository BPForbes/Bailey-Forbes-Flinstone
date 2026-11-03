kernel/core/sched — scheduling and background work

| File | Roadmap | Status |
|------|---------|--------|
| task_manager.c | P1 scheduler | existing |
| priority_queue.c | P1 | existing |
| threadpool.c | userland shell jobs on H | existing (not P1-8) |
| workqueue.c / workqueue.h | P1-8 | MLQ-backed enqueue/poll/drain, delayed + periodic + protected work (uses priority_queue.c) |
| priority_queue.c | P1 / P1-8 | Multilevel queue (threadpool + workqueue) |
| wq_journal_sqlite.c / .h | P1-8 | hosted flushed journal for durable work (FL-25); the default journal is in workqueue.c |
| bg_jobs.c / bg_jobs.h | P1-9, P1-10, P3-14 kick | tick + hosted ticker thread; domain jobs still stubs |

P1-8 workqueues are kernel deferred work; see docs/BACKGROUND_JOBS.md.
P3-13 server chat uses userland pthread in userland/shell (not this directory).
