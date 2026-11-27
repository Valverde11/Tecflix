#pragma once

#include <httplib.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

namespace tecflix::http {

// Default upper bound for HTTP worker threads.
//
// cpp-httplib serves each connection on one pool thread and, with keep-alive, that
// thread stays blocked while the connection is idle waiting for the next request
// (up to 5 s). If there are more connected clients than threads, the extra requests
// wait in the pool queue until the client times out; they never reach a handler, so
// the per-route metrics do not show them. httplib's own default limit is 4x the base
// (60 threads on a 16-thread CPU), which 64 keep-alive clients already exceed.
inline constexpr std::size_t kDefaultMaxThreads = 128;

// Reads TECFLIX_HTTP_MAX_THREADS (positive integer); invalid values fall back to the
// default with a warning. Never returns less than base_threads.
inline std::size_t MaxThreadsFromEnv(std::size_t base_threads) {
    std::string value;
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&raw, &length, "TECFLIX_HTTP_MAX_THREADS") == 0 && raw != nullptr) {
        value = raw;
        std::free(raw);
    }
#else
    if (const char* raw = std::getenv("TECFLIX_HTTP_MAX_THREADS")) value = raw;
#endif
    std::size_t max_threads = kDefaultMaxThreads;
    if (!value.empty()) {
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' || parsed < 1) {
            std::cerr << "TECFLIX_HTTP_MAX_THREADS inválido, se usa " << kDefaultMaxThreads
                      << '\n';
        } else {
            max_threads = static_cast<std::size_t>(parsed);
        }
    }
    return std::max(base_threads, max_threads);
}

// Installs a dynamic pool: httplib's default base size (max(8, cores - 1)) stays
// resident, and extra threads are created on demand up to MaxThreadsFromEnv(); the
// extra ones exit after CPPHTTPLIB_THREAD_POOL_IDLE_TIMEOUT seconds without work.
inline void ConfigureThreadPool(httplib::Server& server) {
    const std::size_t base_threads = CPPHTTPLIB_THREAD_POOL_COUNT;
    const std::size_t max_threads = MaxThreadsFromEnv(base_threads);
    // httplib takes ownership of the returned queue.
    server.new_task_queue = [base_threads, max_threads] {
        return new httplib::ThreadPool(base_threads, max_threads);
    };
}

}  // namespace tecflix::http
