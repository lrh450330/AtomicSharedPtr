#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <array>


template <typename T>
class naive_atomic_shared_ptr_with_mutex {
public:
    naive_atomic_shared_ptr_with_mutex(const std::shared_ptr<T>& p) :pointer(p) {}

    naive_atomic_shared_ptr_with_mutex& operator=(const std::shared_ptr<T>& p) {
        {
            std::lock_guard guard(mutex);
            pointer = p;
        }
        return *this;
    }

    operator std::shared_ptr<T>() const {
        std::lock_guard guard(mutex);
        return pointer;
    }

private:
    mutable std::mutex mutex;
    std::shared_ptr<T> pointer;
};

constexpr int under_construction_label = std::numeric_limits<int>::max() / 2;

template <typename T, size_t ring_size = 4>
class atomic_shared_ptr_with_ring {
public:
    // initialization is not atomic and thread safe
    atomic_shared_ptr_with_ring(const std::shared_ptr<T>& p) {
        pointers[0] = p;
    }

    atomic_shared_ptr_with_ring& operator=(const std::shared_ptr<T>& p) {

        for (;;)
        {
            // choose pointer for writing
            auto idx = current_write_pointer.fetch_add(1) % ring_size;

            // record usage by our thread
            pointer_usage[idx].fetch_add(1);

            if (idx == current_read_pointer) {
                pointer_usage[idx].fetch_sub(1);
                // don't start construction on the active road
                continue;
            }

            // we are in hope that idx pointer is used exclusivly by our thread
            // and put under_construction_label to protect it from usage by other threads
            int expected = 1;
            if (!pointer_usage[idx].compare_exchange_weak(expected, under_construction_label + 1)) {
                pointer_usage[idx].fetch_sub(1);
                // pointer already in use by other thread, try with different pointer
                continue;
            }
            // at this point we obtained exclusive ownership on idx pointer and alowed to modify it
            pointers[idx] = p;
            // degradate usage lock to read-only (must be ordered with previous pointer modification)
            pointer_usage[idx].fetch_sub(under_construction_label);
            // make it readable
            current_read_pointer = idx;
            // release usage by our thread
            pointer_usage[idx].fetch_sub(1);
            return *this;
        }
    }

    operator std::shared_ptr<T>() const {
        std::shared_ptr<T> result;
        for (;;) {
            auto idx = current_read_pointer.load();
            auto usage = pointer_usage[idx].fetch_add(1);

            if (usage >= under_construction_label) {
                pointer_usage[idx].fetch_sub(1);
                continue;
            }

            result = pointers[idx];
            pointer_usage[idx].fetch_sub(1);
            return result;
        }
    }

private:
    std::array<std::shared_ptr<T>, ring_size> pointers;
    mutable std::array<std::atomic<int>, ring_size> pointer_usage = { 0 };
    std::atomic<int> current_read_pointer = { 0 };
    std::atomic<int> current_write_pointer = { 1 % ring_size };
};

const size_t reader_count = 4;
const size_t writer_count = 2;
const size_t iterations = 1000000;
const auto writers_interval = std::chrono::nanoseconds(1);

template<template<typename> typename atomic_shared_ptr>
void run_test() {
    atomic_shared_ptr<size_t> shared_ptr = std::make_shared<size_t>(0);

    std::vector<std::thread> readers(reader_count);
    std::vector<std::thread> writers(writer_count);

    size_t sums[reader_count][writer_count + 1] = { 0 };

    std::atomic_bool enable_writers = true;
    for (size_t writer = 0; writer < writer_count; ++writer) {
        writers[writer] = std::thread([&shared_ptr, &enable_writers, writer]() {
            auto local = std::make_shared<size_t>(writer + 1);
            while (enable_writers) {
                std::this_thread::sleep_for(writers_interval);
                shared_ptr = local;
            }
        });
    }

    auto start = std::chrono::steady_clock::now();

    for (size_t reader = 0; reader < reader_count; ++reader) {
        readers[reader] = std::thread([&shared_ptr, &sums = sums[reader]]() {
            for (size_t i = 0; i < iterations; ++i) {
                std::shared_ptr<size_t> local_ptr = shared_ptr;
                sums[*local_ptr]++;
            }
        });
    }
    for (auto& task : readers) {
        task.join();
    }

    auto end = std::chrono::steady_clock::now();

    std::cout << iterations << " done in " <<
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    enable_writers = false;
    for (auto& task : writers) {
        task.join();
    }

    for (size_t reader = 0; reader < reader_count; ++reader) {
        std::cout << "Reader " << reader << " :";
        for (size_t writer = 0; writer <= writer_count; ++writer) {
            std::cout << " " << 100.0 * sums[reader][writer] / iterations << "% (" << sums[reader][writer] << ")";
        }
        std::cout << "\n";
    }
}

int main()
{
    std::cout << "mutex impl\n";
    run_test<naive_atomic_shared_ptr_with_mutex>();
    run_test<naive_atomic_shared_ptr_with_mutex>();
    run_test<naive_atomic_shared_ptr_with_mutex>();

    std::cout << "ring impl\n";
    run_test<atomic_shared_ptr_with_ring>();
    run_test<atomic_shared_ptr_with_ring>();
    run_test<atomic_shared_ptr_with_ring>();
}

