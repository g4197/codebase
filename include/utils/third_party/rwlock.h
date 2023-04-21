/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RWLOCK_H_
#define RWLOCK_H_

#include <algorithm>
#include <atomic>
#include <thread>

class RWSpinLock {
    enum : int32_t { READER = 4, UPGRADED = 2, WRITER = 1 };

public:
    constexpr RWSpinLock() : bits_(0) {}

    // Lockable Concept
    void lock() {
        uint_fast32_t count = 0;
        while (!likely(try_lock())) {
            if (++count > 1000) {
                std::this_thread::yield();
            }
        }
    }

    // Writer is responsible for clearing up both the UPGRADED and WRITER bits.
    void unlock() {
        static_assert(READER > WRITER + UPGRADED, "wrong bits!");
        bits_payload()->fetch_and(~(WRITER | UPGRADED), std::memory_order_release);
    }

    // SharedLockable Concept
    void lock_shared() {
        uint_fast32_t count = 0;
        while (!likely(try_lock_shared())) {
            if (++count > 1000) {
                std::this_thread::yield();
            }
        }
    }

    void unlock_shared() {
        bits_payload()->fetch_add(-READER, std::memory_order_release);
    }

    // Downgrade the lock from writer status to reader status.
    void unlock_and_lock_shared() {
        bits_payload()->fetch_add(READER, std::memory_order_acquire);
        unlock();
    }

    // UpgradeLockable Concept
    void lock_upgrade() {
        uint_fast32_t count = 0;
        while (!try_lock_upgrade()) {
            if (++count > 1000) {
                std::this_thread::yield();
            }
        }
    }

    void unlock_upgrade() {
        bits_payload()->fetch_add(-UPGRADED, std::memory_order_acq_rel);
    }

    // unlock upgrade and try to acquire write lock
    void unlock_upgrade_and_lock() {
        int64_t count = 0;
        while (!try_unlock_upgrade_and_lock()) {
            if (++count > 1000) {
                std::this_thread::yield();
            }
        }
    }

    // unlock upgrade and read lock atomically
    void unlock_upgrade_and_lock_shared() {
        bits_payload()->fetch_add(READER - UPGRADED, std::memory_order_acq_rel);
    }

    // write unlock and upgrade lock atomically
    void unlock_and_lock_upgrade() {
        // need to do it in two steps here -- as the UPGRADED bit might be OR-ed at
        // the same time when other threads are trying do try_lock_upgrade().
        bits_payload()->fetch_or(UPGRADED, std::memory_order_acquire);
        bits_payload()->fetch_add(-WRITER, std::memory_order_release);
    }

    // Attempt to acquire writer permission. Return false if we didn't get it.
    bool try_lock() {
        int32_t expect = 0;
        return bits_payload()->compare_exchange_strong(expect, WRITER, std::memory_order_acq_rel);
    }

    // Try to get reader permission on the lock. This can fail if we
    // find out someone is a writer or upgrader.
    // Setting the UPGRADED bit would allow a writer-to-be to indicate
    // its intention to write and block any new readers while waiting
    // for existing readers to finish and release their read locks. This
    // helps avoid starving writers (promoted from upgraders).
    bool try_lock_shared() {
        // fetch_add is considerably (100%) faster than compare_exchange,
        // so here we are optimizing for the common (lock success) case.
        int32_t value = bits_payload()->fetch_add(READER, std::memory_order_acquire);
        if (unlikely(value & (WRITER | UPGRADED))) {
            bits_payload()->fetch_add(-READER, std::memory_order_release);
            return false;
        }
        return true;
    }

    // try to unlock upgrade and write lock atomically
    bool try_unlock_upgrade_and_lock() {
        int32_t expect = UPGRADED;
        return bits_payload()->compare_exchange_strong(expect, WRITER, std::memory_order_acq_rel);
    }

    // try to acquire an upgradable lock.
    bool try_lock_upgrade() {
        int32_t value = bits_payload()->fetch_or(UPGRADED, std::memory_order_acquire);

        // Note: when failed, we cannot flip the UPGRADED bit back,
        // as in this case there is either another upgrade lock or a write lock.
        // If it's a write lock, the bit will get cleared up when that lock's done
        // with unlock().
        return ((value & (UPGRADED | WRITER)) == 0);
    }

    // mainly for debugging purposes.
    int32_t bits() const {
        return bits_payload()->load(std::memory_order_acquire);
    }

    std::atomic<int32_t> *bits_payload() {
        return reinterpret_cast<std::atomic<int32_t> *>(&bits_);
    }

    const std::atomic<int32_t> *bits_payload() const {
        return reinterpret_cast<const std::atomic<int32_t> *>(&bits_);
    }

    class ReadHolder;
    class UpgradedHolder;
    class WriteHolder;

    class ReadHolder {
    public:
        explicit ReadHolder(RWSpinLock *lock) : lock_(lock) {
            if (lock_) {
                lock_->lock_shared();
            }
        }

        explicit ReadHolder(RWSpinLock &lock) : lock_(&lock) {
            lock_->lock_shared();
        }

        ReadHolder(ReadHolder &&other) noexcept : lock_(other.lock_) {
            other.lock_ = nullptr;
        }

        // down-grade
        explicit ReadHolder(UpgradedHolder &&upgraded) : lock_(upgraded.lock_) {
            upgraded.lock_ = nullptr;
            if (lock_) {
                lock_->unlock_upgrade_and_lock_shared();
            }
        }

        explicit ReadHolder(WriteHolder &&writer) : lock_(writer.lock_) {
            writer.lock_ = nullptr;
            if (lock_) {
                lock_->unlock_and_lock_shared();
            }
        }

        ReadHolder &operator=(ReadHolder &&other) {
            using std::swap;
            swap(lock_, other.lock_);
            return *this;
        }

        ReadHolder(const ReadHolder &other) = delete;
        ReadHolder &operator=(const ReadHolder &other) = delete;

        ~ReadHolder() {
            if (lock_) {
                lock_->unlock_shared();
            }
        }

        void reset(RWSpinLock *lock = nullptr) {
            if (lock == lock_) {
                return;
            }
            if (lock_) {
                lock_->unlock_shared();
            }
            lock_ = lock;
            if (lock_) {
                lock_->lock_shared();
            }
        }

        void swap(ReadHolder *other) {
            std::swap(lock_, other->lock_);
        }

    private:
        friend class UpgradedHolder;
        friend class WriteHolder;
        RWSpinLock *lock_;
    };

    class UpgradedHolder {
    public:
        explicit UpgradedHolder(RWSpinLock *lock) : lock_(lock) {
            if (lock_) {
                lock_->lock_upgrade();
            }
        }

        explicit UpgradedHolder(RWSpinLock &lock) : lock_(&lock) {
            lock_->lock_upgrade();
        }

        explicit UpgradedHolder(WriteHolder &&writer) {
            lock_ = writer.lock_;
            writer.lock_ = nullptr;
            if (lock_) {
                lock_->unlock_and_lock_upgrade();
            }
        }

        UpgradedHolder(UpgradedHolder &&other) noexcept : lock_(other.lock_) {
            other.lock_ = nullptr;
        }

        UpgradedHolder &operator=(UpgradedHolder &&other) {
            using std::swap;
            swap(lock_, other.lock_);
            return *this;
        }

        UpgradedHolder(const UpgradedHolder &other) = delete;
        UpgradedHolder &operator=(const UpgradedHolder &other) = delete;

        ~UpgradedHolder() {
            if (lock_) {
                lock_->unlock_upgrade();
            }
        }

        void reset(RWSpinLock *lock = nullptr) {
            if (lock == lock_) {
                return;
            }
            if (lock_) {
                lock_->unlock_upgrade();
            }
            lock_ = lock;
            if (lock_) {
                lock_->lock_upgrade();
            }
        }

        void swap(UpgradedHolder *other) {
            using std::swap;
            swap(lock_, other->lock_);
        }

    private:
        friend class WriteHolder;
        friend class ReadHolder;
        RWSpinLock *lock_;
    };

    class WriteHolder {
    public:
        explicit WriteHolder(RWSpinLock *lock) : lock_(lock) {
            if (lock_) {
                lock_->lock();
            }
        }

        explicit WriteHolder(RWSpinLock &lock) : lock_(&lock) {
            lock_->lock();
        }

        // promoted from an upgrade lock holder
        explicit WriteHolder(UpgradedHolder &&upgraded) {
            lock_ = upgraded.lock_;
            upgraded.lock_ = nullptr;
            if (lock_) {
                lock_->unlock_upgrade_and_lock();
            }
        }

        WriteHolder(WriteHolder &&other) noexcept : lock_(other.lock_) {
            other.lock_ = nullptr;
        }

        WriteHolder &operator=(WriteHolder &&other) {
            using std::swap;
            swap(lock_, other.lock_);
            return *this;
        }

        WriteHolder(const WriteHolder &other) = delete;
        WriteHolder &operator=(const WriteHolder &other) = delete;

        ~WriteHolder() {
            if (lock_) {
                lock_->unlock();
            }
        }

        void reset(RWSpinLock *lock = nullptr) {
            if (lock == lock_) {
                return;
            }
            if (lock_) {
                lock_->unlock();
            }
            lock_ = lock;
            if (lock_) {
                lock_->lock();
            }
        }

        void swap(WriteHolder *other) {
            using std::swap;
            swap(lock_, other->lock_);
        }

    private:
        friend class ReadHolder;
        friend class UpgradedHolder;
        RWSpinLock *lock_;
    };

private:
    int32_t bits_;
};

#endif  // RWLOCK_H_