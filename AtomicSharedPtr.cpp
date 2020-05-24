// AtomicSharedPtr.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

template <typename T>
class naive_atomic_shared_ptr_with_mutex {
public:
    naive_atomic_shared_ptr_with_mutex(std::shared_ptr<T> p) :pointer(p) {}

    naive_atomic_shared_ptr_with_mutex& operator=(std::shared_ptr<T> p) {
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

template <typename T>
using atomic_shared_ptr = naive_atomic_shared_ptr_with_mutex<T>;

const size_t reader_count = 4;
const size_t writer_count = 4;
const size_t iterations = 10000000;

int main()
{
    atomic_shared_ptr<size_t> shared_ptr = std::make_shared<size_t>(0);

    std::vector<std::thread> readers(reader_count);
    std::vector<std::thread> writers(writer_count);

    size_t sums[reader_count][writer_count + 1] = { 0 };

    auto start = std::chrono::steady_clock::now();

    for (size_t reader = 0; reader < reader_count; ++reader) {
        readers[reader] = std::thread([&shared_ptr, &sums = sums[reader]]() {
            for (size_t i = 0; i < iterations; ++i) {
                std::shared_ptr<size_t> local_ptr = shared_ptr;
                sums[*local_ptr]++;
            }
        });
    }

    for (size_t writer = 0; writer < writer_count; ++writer) {
        writers[writer] = std::thread([&shared_ptr, writer]() {
            auto local = std::make_shared<size_t>(writer + 1);
            for (size_t i = 0; i < iterations; ++i) {
                shared_ptr = local;
            }
        });
    }

    for (auto tasks_ptr : { &readers, &writers }) {
        for (auto& task : *tasks_ptr) {
            task.join();
        }
    }

    auto end = std::chrono::steady_clock::now();

    std::cout << iterations << " done in " <<
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    for (size_t reader = 0; reader < reader_count; ++reader) {
        std::cout << "Reader " << reader << " :";
        for (size_t writer = 0; writer <= writer_count; ++writer) {
            std::cout << " " << 100.0 * sums[reader][writer] / iterations << "% (" << sums[reader][writer] << ")";
        }
        std::cout << "\n";
    }
}

