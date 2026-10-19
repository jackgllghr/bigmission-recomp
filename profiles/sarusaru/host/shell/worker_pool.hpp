#pragma once

// A few threads for work the renderer must not wait for (HD image decode,
// translation lettering, pipeline warm-up). Tasks run in the order queued;
// destruction drops tasks not yet started and waits for the running ones.
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sarusaru::shell {

class WorkerPool {
public:
    // Half the cores (the game and audio keep the rest busy), 1 to 4.
    static unsigned default_threads() {
        return std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 4u);
    }

    explicit WorkerPool(unsigned threads = default_threads()) {
        for (unsigned i = 0; i < threads; ++i) threads_.emplace_back([this] { run(); });
    }
    ~WorkerPool() { stop(); }
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;

    void post(std::function<void()> task) {
        {
            const std::lock_guard lock(mutex_);
            if (stopping_) return;
            tasks_.push_back(std::move(task));
        }
        ready_.notify_one();
    }

    void stop() {
        {
            const std::lock_guard lock(mutex_);
            if (stopping_ && threads_.empty()) return;
            stopping_ = true;
            tasks_.clear();
        }
        ready_.notify_all();
        for (auto &thread : threads_) thread.join();
        threads_.clear();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_) return;
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> threads_;
    bool stopping_{};
};

} // namespace sarusaru::shell
