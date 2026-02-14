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
#include <iostream>
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
        std::cout << "[Scheduler] init workers=" << workerCount
                  << " maxQueueSize=" << maxQueueSize_ << "\n";
        for (std::size_t i = 0; i < workerCount; ++i) { workers_.emplace_back(&Scheduler::workerLoop, this); }
    }
    ~Scheduler() {
        shutdown(ShutdownMode::Immediate);
    }

    // Multi-producer API.
    // Returns nullopt if queue is full or scheduler is shutting down.
    std::optional<JobId> schedule(JobFn job, TimePoint runAt, Priority priority) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if(!accepting_ or queue_.size() >= maxQueueSize_) {
            std::cout << "[Scheduler] schedule rejected accepting=" << accepting_
                      << " queueSize=" << queue_.size() << "\n";
            return std::nullopt;
        }
        JobId currId = nextId_++;
        queue_.push(Job{currId, runAt, priority, std::move(job), Clock::now()});
        std::cout << "[Scheduler] schedule id=" << currId
                  << " queueSize=" << queue_.size() << "\n";
        queueCv_.notify_one();
        return currId;
    }
    bool cancel(JobId id) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if(!accepting_) {
            std::cout << "[Scheduler] cancel rejected id=" << id << " (not accepting)\n";
            return false;
        }
        cancelled_[id] = true;
        std::cout << "[Scheduler] cancel marked id=" << id << "\n";
        return true;
    }

    // Shutdown API.
    void shutdown(ShutdownMode mode) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        std::cout << "[Scheduler] shutdown requested mode="
                  << (mode == ShutdownMode::Immediate ? "Immediate" : "Graceful")
                  << " queueSize=" << queue_.size() << "\n";
        accepting_ = false;
        shutdownMode_ = mode;
        if(mode == ShutdownMode::Immediate) {
            // Clear pending jobs.
            while(!queue_.empty()) { queue_.pop(); }
            stopWorkers_ = true;
            std::cout << "[Scheduler] immediate shutdown: pending jobs dropped\n";
        } else {
            // If already empty, graceful shutdown can stop immediately.
            if(queue_.empty()) { stopWorkers_ = true; }
        }
        lock.unlock();
        queueCv_.notify_all();
        joinWorkers();
        
    }

    // Metrics snapshot.
    SchedulerMetrics metrics() const {
        SchedulerMetrics sm;
        std::lock_guard<std::mutex> lock(queueMutex_);
        const auto completed = completedJobs_.load();
        const auto totalWaitNs = totalWaitNs_.load();
        sm.queuedJobs = queue_.size();
        sm.runningJobs = runningJobs_.load();
        sm.avgWaitMs = completed > 0
            ? (static_cast<double>(totalWaitNs) / static_cast<double>(completed)) / 1e6
            : 0.0;
        return sm;
    }

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
    void workerLoop() {
        std::cout << "[Worker " << std::this_thread::get_id() << "] started\n";
        while (true) {
            std::unique_lock<std::mutex> lock(queueMutex_);

            Job job{};
            while (true) {
                if (stopWorkers_) { return; }

                if (queue_.empty()) {
                    if (!accepting_ && shutdownMode_ == ShutdownMode::Graceful) {
                        stopWorkers_ = true;
                        queueCv_.notify_all();
                        std::cout << "[Worker " << std::this_thread::get_id()
                                  << "] graceful stop: queue drained\n";
                        return;
                    }

                    queueCv_.wait(lock, [this] {
                        return stopWorkers_ || !queue_.empty() || (!accepting_ && shutdownMode_ == ShutdownMode::Graceful);
                    });
                    continue;
                }

                const TimePoint nextRunAt = queue_.top().runAt;
                if (nextRunAt > Clock::now()) {
                    std::cout << "[Worker " << std::this_thread::get_id()
                              << "] waiting for next job\n";
                    queueCv_.wait_until(lock, nextRunAt, [this, nextRunAt] {
                        return stopWorkers_ || queue_.empty() || queue_.top().runAt < nextRunAt;
                    });
                    continue;
                }

                job = queue_.top();
                queue_.pop();

                if (cancelled_.erase(job.id) > 0) {
                    std::cout << "[Worker " << std::this_thread::get_id()
                              << "] skipping cancelled job id=" << job.id << "\n";
                    continue;
                }

                break;
            }

            lock.unlock();
            runningJobs_.fetch_add(1);
            std::cout << "[Worker " << std::this_thread::get_id()
                      << "] running job id=" << job.id << "\n";
            try {
                job.fn();
            } catch(...) {
                // Handle exceptions gracefully.
                // In a real system, this would be logged or handled appropriately.
                std::cout << "[Worker " << std::this_thread::get_id()
                          << "] job id=" << job.id << " threw exception\n";
            }

            const auto waitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - job.enqueuedAt).count();
            if (waitNs > 0) {
                totalWaitNs_.fetch_add(static_cast<std::uint64_t>(waitNs));
            }
            completedJobs_.fetch_add(1);
            runningJobs_.fetch_sub(1);
            std::cout << "[Worker " << std::this_thread::get_id()
                      << "] completed job id=" << job.id << "\n";
        }
    }

    // Helper for shutdown sequence and join.
    void joinWorkers() {
        for(auto& worker : workers_) {
            if(worker.joinable()) {
                worker.join();
            }
        }
    }
};
