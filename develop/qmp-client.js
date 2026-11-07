((root, factory) => {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.FlintstoneQmp = api;
})(typeof globalThis === "object" ? globalThis : this, () => {
  "use strict";

  function createQmpClient({ send, timeoutMs = 30000 }) {
    let nextId = 0;
    let phase = "init";
    let inflight = null;
    const queue = [];
    const pending = new Map();

    function finish(job, error, value) {
      if (job.timer) clearTimeout(job.timer);
      pending.delete(job.id);
      if (inflight === job) inflight = null;
      if (error) job.reject(error);
      else job.resolve(value);
      pump();
    }

    function pump() {
      if (inflight || !queue.length || phase === "init") return;
      if (phase === "negotiating") {
        const idx = queue.findIndex(job => job.execute === "qmp_capabilities");
        if (idx < 0) return;
        if (idx > 0) queue.unshift(queue.splice(idx, 1)[0]);
      }
      const job = queue.shift();
      inflight = job;
      const id = ++nextId;
      job.id = id;
      job.timer = setTimeout(() => {
        if (pending.get(id) !== job) return;
        finish(job, new Error(`QEMU command timed out: ${job.execute}`));
      }, job.timeoutMs || timeoutMs);
      pending.set(id, job);
      send({ execute: job.execute, ...(job.args ? { arguments: job.args } : {}), id });
    }

    return {
      command(execute, args, commandTimeoutMs) {
        return new Promise((resolve, reject) => {
          queue.push({ execute, args, timeoutMs: commandTimeoutMs || timeoutMs, resolve, reject });
          pump();
        });
      },
      accept(data) {
        if (data && data.QMP) {
          if (phase === "init") phase = "negotiating";
          pump();
          return "greeting";
        }
        const job = data && pending.get(data.id);
        if (!job) return data && data.event ? "event" : "ignored";
        if (data.error) finish(job, new Error((data.error && data.error.desc) || "QMP error"));
        else finish(job, null, data.return);
        return "reply";
      },
      allowWork() {
        phase = "ready";
        pump();
      },
      failAll(error) {
        const err = error instanceof Error ? error : new Error(String(error || "QEMU powered off"));
        const jobs = [...pending.values(), ...queue];
        pending.clear();
        queue.length = 0;
        inflight = null;
        for (const job of jobs) {
          if (job.timer) clearTimeout(job.timer);
          job.reject(err);
        }
      },
    };
  }

  return { createQmpClient };
});
