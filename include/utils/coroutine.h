#ifndef COROUTINE_H_
#define COROUTINE_H_

#include <glog/logging.h>

#include <boost/coroutine2/all.hpp>
#include <vector>

struct Coroutine {
    using coro_t = boost::coroutines2::coroutine<void>;
    coro_t::pull_type fn_;
    coro_t::push_type *yield_;

    template<class Function, class... Args>
    Coroutine(Function &&f, Args... args)
        : fn_([&](coro_t::push_type &yield) {
              this->yield_ = &yield;
              yield();
              f(args...);
          }) {}

    inline bool finished() {
        return !fn_;
    }

    inline void run() {
        fn_();
    }

    inline void operator()() {
        run();
    }

    inline void yield() {
        (*yield_)();
    }
};

constexpr uint64_t kMaxCoroutines = 32;

struct CoroScheduler {
    using coro_t = boost::coroutines2::coroutine<void>;
    int cur_index_;
    int coro_size_;
    coro_t::push_type *yield_;
    Coroutine *coros_[kMaxCoroutines];

    template<class Function, class... Args>
    inline void insert(Function &&f, Args &&...args) {
        coros_[coro_size_++] = new Coroutine(f, args...);
    }

    inline size_t size() {
        return coro_size_;
    }

    inline bool next() {
        int last_index = cur_index_;
        do {
            cur_index_ = (cur_index_ + 1) % coro_size_;
            this->yield_ = coros_[cur_index_]->yield_;
            if (!coros_[cur_index_]->finished()) {
                coros_[cur_index_]->run();
                return true;
            }
        } while (cur_index_ != last_index);
        return false;
    }

    inline void yield() {
        (*yield_)();
    }
};

extern thread_local CoroScheduler coro_scheduler;

inline void coro_yield() {
    coro_scheduler.yield();
}

#endif  // COROUTINE_H_