#include "Utils.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#include <mach/mach.h>
#endif

double getCurrentTime() {
    return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

double getCurrentTimeNative() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return counter.QuadPart / (double)freq.QuadPart;
#elif defined(__APPLE__)
    uint64_t time = mach_absolute_time();
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    return (time * timebase.numer / timebase.denom) / 1e9;
#else
    // For other platforms, use SDL
    return getCurrentTime();
#endif
}

std::pair<long long, long long> getCpuTimes() {
#ifdef _WIN32
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idleTime.dwLowDateTime;
        idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime;
        kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime;
        user.HighPart = userTime.dwHighDateTime;
        long long total = kernel.QuadPart + user.QuadPart;
        long long idle_t = idle.QuadPart;
        return {total, idle_t};
    } else {
        return {0, 0};
    }
#elif defined(__APPLE__)
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        long long user = cpuinfo.cpu_ticks[CPU_STATE_USER];
        long long system = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
        long long idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        long long nice = cpuinfo.cpu_ticks[CPU_STATE_NICE];
        long long total = user + system + idle + nice;
        return {total, idle};
    } else {
        return {0, 0};
    }
#else
    // Linux
    long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    long long total_time, idle_time;

    std::ifstream f("/proc/stat");
    std::string line;
    std::getline(f, line); // Read the "cpu" line

    // Parse the CPU times
    std::istringstream ss(line);
    std::string cpu_label;
    ss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;

    idle_time = idle + iowait;
    total_time = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;

    return {total_time, idle_time};
#endif
}