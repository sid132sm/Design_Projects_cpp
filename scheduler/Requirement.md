<!-- / =========================
// Design / Implementation Outline
// =========================
//
// 1) schedule()
//    - Lock queueMutex_.
//    - Reject if !accepting_.
//    - Enforce maxQueueSize_ (backpressure).
//    - Allocate JobId via nextId_.
//    - Push Job to priority queue, set enqueuedAt=Clock::now().
//    - Unlock + notify one/all workers.
//
// 2) cancel(JobId)
//    - Lock queueMutex_.
//    - Mark cancelled_[id] = true.
//    - Do not remove from heap in O(n); lazy-skip in workerLoop().
//
// 3) workerLoop()
//    - Hold unique_lock<mutex> lk(queueMutex_).
//    - Wait on queueCv_ with predicate:
//      * stopWorkers_, or
//      * queue not empty and top job is ready (runAt <= now), or
//      * queue not empty and need timed wait until top.runAt.
//    - If stopWorkers_ and (mode immediate OR queue empty), exit thread.
//    - Pop top job, skip if cancelled.
//    - Increment runningJobs_, unlock, execute fn() in try/catch.
//    - On completion, compute wait time and update metrics atomics.
//    - Lock again, decrement runningJobs_.
//
// 4) shutdown(mode)
//    - Lock queueMutex_.
//    - accepting_ = false.
//    - shutdownMode_ = mode.
//    - If mode == Immediate:
//      * clear pending queue_ (drop jobs)
//      * stopWorkers_ = true
//    - Else Graceful:
//      * stopWorkers_ = true only after queue drains (worker side check)
//        or use a separate "drain then stop" condition.
//    - Unlock and notify_all.
//    - Join all worker threads exactly once.
//
// 5) metrics()
//    - queuedJobs: lock queueMutex_ and read queue_.size().
//    - runningJobs: atomic load.
//    - avgWaitMs: totalWaitNs / completedJobs (guard divide-by-zero).
//
// 6) Concurrency design notes
//    - Keep job execution outside locks.
//    - Use one queue mutex first; split locks only if contention proves high.
//    - Lazy cancellation keeps hot path simple.
//    - Prefer steady_clock for scheduling.
//
// 7) Test plan (later)
//    - Multi-producer schedule + cancel race.
//    - Priority ordering with same runAt.
//    - Timed scheduling correctness.
//    - Graceful vs immediate shutdown behavior.
//    - Exception in job does not kill worker thread.
//    - Queue limit/backpressure behavior. -->
