#include "thread.h"

#include <cerrno>
#include <cstdint>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace mediagrep {

namespace {

// Stack sizes to try, largest first. Only touched pages are committed, so a
// big reservation costs address space, not memory; the fallbacks are for
// 32-bit systems where many workers would not fit.
constexpr std::size_t kStackSizes[] = {256u << 20, 64u << 20, 16u << 20};

// Bytes of stack budgeted per character of regex input, for the patterns
// libstdc++ must run recursively (those with back-references; the rest use
// its non-recursive executor, see eval.cpp). Measured: 'a.*b' uses about 80
// bytes per character and '(a|b)*b' about 400.
constexpr std::size_t kStackPerChar = 1024;

#if defined(__GLIBCXX__)
// Threads not started by WorkerThread: assume the common 8 MB default.
constexpr std::size_t kDefaultLimit = (8u << 20) / kStackPerChar;
std::size_t limitForStack(std::size_t stack) {
    return stack ? stack / kStackPerChar : kDefaultLimit;
}
#else
// libc++ and MSVC do not recurse per character; MSVC throws regex_error
// when it gives up, which the evaluator reports.
constexpr std::size_t kDefaultLimit = SIZE_MAX;
std::size_t limitForStack(std::size_t) { return SIZE_MAX; }
#endif

thread_local std::size_t regexLimit = kDefaultLimit;

}  // namespace

std::size_t threadRegexLimit() { return regexLimit; }
void setThreadRegexLimit(std::size_t chars) { regexLimit = chars; }

struct WorkerThread::Impl {
    std::function<void()> fn;
    std::size_t stack = 0;  // 0: platform default
    bool joined = false;
#ifdef _WIN32
    HANDLE handle = nullptr;
#else
    pthread_t id{};
#endif

    void run() {
        setThreadRegexLimit(limitForStack(stack));
        fn();
    }
};

#ifdef _WIN32

namespace {
unsigned __stdcall trampoline(void* arg) {
    static_cast<WorkerThread::Impl*>(arg)->run();
    return 0;
}
}  // namespace

WorkerThread::WorkerThread(std::function<void()> fn) : impl_(std::make_unique<Impl>()) {
    impl_->fn = std::move(fn);
    for (std::size_t size : kStackSizes) {
        impl_->stack = size;
        const std::uintptr_t h =
            _beginthreadex(nullptr, static_cast<unsigned>(size), trampoline, impl_.get(),
                           STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (h) {
            impl_->handle = reinterpret_cast<HANDLE>(h);
            return;
        }
    }
    impl_->stack = 0;
    const std::uintptr_t h = _beginthreadex(nullptr, 0, trampoline, impl_.get(), 0, nullptr);
    if (!h) throw std::system_error(errno, std::generic_category(), "cannot start thread");
    impl_->handle = reinterpret_cast<HANDLE>(h);
}

void WorkerThread::join() {
    if (impl_->joined) return;
    WaitForSingleObject(impl_->handle, INFINITE);
    CloseHandle(impl_->handle);
    impl_->joined = true;
}

#else

namespace {
void* trampoline(void* arg) {
    static_cast<WorkerThread::Impl*>(arg)->run();
    return nullptr;
}
}  // namespace

WorkerThread::WorkerThread(std::function<void()> fn) : impl_(std::make_unique<Impl>()) {
    impl_->fn = std::move(fn);
    for (std::size_t size : kStackSizes) {
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0) break;
        impl_->stack = size;
        const bool started = pthread_attr_setstacksize(&attr, size) == 0 &&
                             pthread_create(&impl_->id, &attr, trampoline, impl_.get()) == 0;
        pthread_attr_destroy(&attr);
        if (started) return;
    }
    impl_->stack = 0;
    const int rc = pthread_create(&impl_->id, nullptr, trampoline, impl_.get());
    if (rc != 0) throw std::system_error(rc, std::generic_category(), "cannot start thread");
}

void WorkerThread::join() {
    if (impl_->joined) return;
    pthread_join(impl_->id, nullptr);
    impl_->joined = true;
}

#endif

WorkerThread::~WorkerThread() { join(); }

}  // namespace mediagrep
