// scheduler.cpp
// Design outline for a thread-safe job scheduler (C++17/20).
// Intent: keep this file as a blueprint; implementation can be added step-by-step.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using JobId = std::uint64_t;

enum class Priority : std::uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
};

using JobFn = std::function<void()>;

enum class ShutdownMode : std::uint8_t {
    Graceful,     // finish running + pending jobs
    Immediate,    // stop taking new jobs, drop pending jobs
};

struct Job {
    JobId id{};
    TimePoint runAt{};
    Priority priority{Priority::Normal};
    JobFn fn{};

    // Optional metadata for metrics/tracing.
    TimePoint enqueuedAt{};
};

// Ready ordering:
// 1) Earlier runAt first
// 2) If same runAt, higher priority first
// 3) Tie-break by lower id first for deterministic behavior
struct JobCompare {
    bool operator()(const Job& a, const Job& b) const {
        if (a.runAt != b.runAt) return a.runAt > b.runAt;
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.id > b.id;
    }
};

struct SchedulerMetrics {
    std::size_t queuedJobs{0};
    std::size_t runningJobs{0};
    double avgWaitMs{0.0};
};

class Scheduler {
public:
    explicit Scheduler(std::size_t workerCount, std::size_t maxQueueSize) {
        maxQueueSize_ = maxQueueSize;
        for (std::size_t i = 0; i < workerCount; ++i) { workers_.emplace_back(&Scheduler::workerLoop, this); }
    }
    ~Scheduler() {
        shutdown(ShutdownMode::Immediate);
    }

    // Multi-producer API.
    // Returns nullopt if queue is full or scheduler is shutting down.
    std::optional<JobId> schedule(JobFn job, TimePoint runAt, Priority priority) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if(!accepting_ or queue_.size() >= maxQueueSize_) { return std::nullopt; }
        JobId currId = nextId_++;
        queue_.push(Job{currId, runAt, priority, std::move(job), Clock::now()});
        queueCv_.notify_one();
        return currId;
    }
    bool cancel(JobId id) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if(!accepting_) { return false; }
        cancelled_[id] = true;
        return true;
    }

    // Shutdown API.
    void shutdown(ShutdownMode mode) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        accepting_ = false;
        shutdownMode_ = mode;
        
    }

    // Metrics snapshot.
    SchedulerMetrics metrics() const;

private:
    // ==== Core state ====
    std::size_t maxQueueSize_{0};
    std::atomic<JobId> nextId_{1};

    // Guard all queue/cancel map/shutdown flags with queueMutex_.
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::priority_queue<Job, std::vector<Job>, JobCompare> queue_;
    std::unordered_map<JobId, bool> cancelled_; // true means cancelled

    bool accepting_{true};
    bool stopWorkers_{false};
    ShutdownMode shutdownMode_{ShutdownMode::Graceful};

    // Worker pool.
    std::vector<std::thread> workers_;

    // Metrics (can be atomics to reduce lock contention for reads).
    std::atomic<std::size_t> runningJobs_{0};
    std::atomic<std::uint64_t> completedJobs_{0};
    std::atomic<std::uint64_t> totalWaitNs_{0};

private:
    // Worker loop:
    // - Wait until next job becomes ready or stop condition.
    // - Pop ready non-cancelled job.
    // - Execute outside lock with exception safety.
    void workerLoop();

    // Helper for shutdown sequence and join.
    void joinWorkers();
};

// =========================
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
//    - Queue limit/backpressure behavior.
