kernel/core/sched — scheduling and background work

| File | Roadmap | Status |
|------|---------|--------|
| task_manager.c | P1 scheduler | existing |
| priority_queue.c | P1 | existing |
| threadpool.c | userland shell jobs on H | existing (not P1-8) |
| workqueue.c / workqueue.h | P1-8 | scaffold: enqueue/poll/drain |
| bg_jobs.c / bg_jobs.h | P1-9, P1-10, P3-14 kick | scaffold: tick + domain stubs |

P1-8 workqueues are kernel deferred work; see docs/BACKGROUND_JOBS.md.
P3-13 server chat uses userland pthread in userland/shell (not this directory).
