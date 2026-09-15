// Where is the game thread, while the VI ticks and nothing is drawn?
//
// The counters in src/renderer.cpp answer "is anything being submitted"
// (no). They cannot answer "what is the game doing instead", and the three
// possibilities need different fixes: spinning in a loop, blocked forever on a
// queue that never fills, or quietly finished with nothing left to run.
//
// This samples every thread in the process periodically and reports two things:
//
//   executing   -- instruction pointers that landed in our own module. A thread
//                  spinning in game code shows up here in bulk.
//   on parked stacks -- addresses found on the stack of threads whose Rip is
//                  inside a system wait. For a blocked thread the Rip is in
//                  ntdll and says nothing; the return address further up is the
//                  call site that matters.
//
// Feed an offset to llvm-symbolizer against the RelWithDebInfo build to name it:
//
//     llvm-symbolizer --obj=body-harvest-recomp.exe --relative-address --demangle <rva>
//
// The stack pass is a frequency hint rather than a stack trace: stale slots
// below the live frame look exactly like live return addresses, so it
// over-reports. What makes it useful is repetition -- an address dominating
// hundreds of samples of a stuck thread is where that thread is stuck.
//
// SAFETY. Suspending a thread and doing anything non-trivial before resuming it
// is a classic deadlock: if the suspended thread holds the CRT or heap lock, the
// sampler blocks forever the moment it allocates or prints. So the loop
// suspends, reads the context, copies a bounded stack window, and resumes --
// touching no lock in between. Everything else happens with all threads running.

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>

namespace {

std::atomic<bool> g_running{false};
std::thread g_sampler;

// How far up a blocked thread's stack to look for return addresses.
//
// 256 words (2 KB) was enough while the failure was in boot scaffolding, and
// stopped being enough as soon as the game itself started running: recompiled
// functions carry the whole MIPS frame plus a recomp_context, so a chain ten
// deep buries the frames that matter well past 2 KB. A pass at that size
// reported only the idle thread and no game code at all, which reads as "the
// game is nowhere" rather than "the window was too small".
constexpr size_t kStackWords = 8192;

// True if the address belongs to our own executable rather than a system DLL.
bool in_main_module(const void* addr, HMODULE main_module) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(addr), &m)) {
        return false;
    }
    return m == main_module;
}

// Copy a bounded window of a suspended thread's stack. Reads only, and asks
// VirtualQuery first so a thread whose Rsp is not in committed memory cannot
// fault the sampler.
size_t copy_stack_window(uintptr_t rsp, uintptr_t* out) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<void*>(rsp), &mbi, sizeof(mbi)) == 0
        || mbi.State != MEM_COMMIT) {
        return 0;
    }
    const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    size_t n = 0;
    for (uintptr_t p = rsp; p + sizeof(uintptr_t) <= end && n < kStackWords;
         p += sizeof(uintptr_t)) {
        out[n++] = *reinterpret_cast<const uintptr_t*>(p);
    }
    return n;
}

void sample_once(DWORD own_tid, HMODULE main_module,
                 std::map<uintptr_t, uint64_t>& executing,
                 std::map<uintptr_t, uint64_t>& on_stacks,
                 uint64_t& parked) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    const DWORD pid = GetCurrentProcessId();

    // Gathered while suspended, analysed once every thread runs again.
    std::vector<void*> ips;
    std::vector<uintptr_t> words;

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == own_tid) {
                continue;
            }
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                                  FALSE, te.th32ThreadID);
            if (h == nullptr) {
                continue;
            }
            if (SuspendThread(h) != (DWORD)-1) {
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL;
                const bool ok = GetThreadContext(h, &ctx);

                uintptr_t window[kStackWords];
                size_t copied = 0;
                if (ok && !in_main_module(reinterpret_cast<void*>(ctx.Rip), main_module)) {
                    copied = copy_stack_window(ctx.Rsp, window);
                }
                ResumeThread(h);

                if (ok) {
                    ips.push_back(reinterpret_cast<void*>(ctx.Rip));
                    for (size_t i = 0; i < copied; ++i) {
                        words.push_back(window[i]);
                    }
                }
            }
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    for (size_t i = 0; i < ips.size(); ++i) {
        if (in_main_module(ips[i], main_module)) {
            executing[reinterpret_cast<uintptr_t>(ips[i])
                      - reinterpret_cast<uintptr_t>(main_module)]++;
        }
        else {
            ++parked;
        }
    }
    for (size_t i = 0; i < words.size(); ++i) {
        const uintptr_t w = words[i];
        if (w != 0 && in_main_module(reinterpret_cast<const void*>(w), main_module)) {
            on_stacks[w - reinterpret_cast<uintptr_t>(main_module)]++;
        }
    }
}

void report_table(const char* title, const std::map<uintptr_t, uint64_t>& m, size_t limit) {
    if (m.empty()) {
        std::fprintf(stderr, "[bh]   %s: (none)\n", title);
        return;
    }
    std::vector<std::pair<uintptr_t, uint64_t> > top(m.begin(), m.end());
    std::sort(top.begin(), top.end(),
              [](const std::pair<uintptr_t, uint64_t>& a,
                 const std::pair<uintptr_t, uint64_t>& b) { return a.second > b.second; });
    std::fprintf(stderr, "[bh]   %s:\n", title);
    const size_t shown = top.size() < limit ? top.size() : limit;
    for (size_t i = 0; i < shown; ++i) {
        std::fprintf(stderr, "[bh]     %6llu  body-harvest-recomp.exe+0x%llx\n",
                     (unsigned long long)top[i].second,
                     (unsigned long long)top[i].first);
    }
}

void report(uint64_t rounds, uint64_t parked,
            const std::map<uintptr_t, uint64_t>& executing,
            const std::map<uintptr_t, uint64_t>& on_stacks) {
    uint64_t running = 0;
    for (std::map<uintptr_t, uint64_t>::const_iterator it = executing.begin();
         it != executing.end(); ++it) {
        running += it->second;
    }
    std::fprintf(stderr,
                 "[bh] thread sample: %llu rounds, %llu executing our code, "
                 "%llu parked in system waits\n",
                 (unsigned long long)rounds, (unsigned long long)running,
                 (unsigned long long)parked);
    report_table("executing", executing, 6);
    report_table("return addresses on parked stacks", on_stacks, 40);
}

} // namespace

void bh_start_thread_sampler() {
    if (std::getenv("BH_SAMPLE") == nullptr) {
        return;   // opt-in: suspending every thread is intrusive
    }
    if (g_running.exchange(true)) {
        return;
    }

    g_sampler = std::thread([] {
        const DWORD own = GetCurrentThreadId();
        HMODULE main_module = GetModuleHandleA(nullptr);

        std::map<uintptr_t, uint64_t> executing, on_stacks;
        uint64_t parked = 0, rounds = 0;
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();

        std::fprintf(stderr, "[bh] thread sampler running (BH_SAMPLE set)\n");

        while (g_running.load(std::memory_order_relaxed)) {
            sample_once(own, main_module, executing, on_stacks, parked);
            ++rounds;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Every two seconds; Rayman 2 reported every second because its game survived
            // only about two (the sampler comes from Rayman 2: Recompiled).
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if (now - last >= std::chrono::seconds(2)) {
                report(rounds, parked, executing, on_stacks);
                last = now;
            }
        }
        report(rounds, parked, executing, on_stacks);
    });
}

void bh_stop_thread_sampler() {
    if (!g_running.exchange(false)) {
        return;
    }
    if (g_sampler.joinable()) {
        g_sampler.join();
    }
}

#else

void bh_start_thread_sampler() {}
void bh_stop_thread_sampler() {}

#endif
