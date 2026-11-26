#pragma once
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace shr {

struct Region {
    std::uintptr_t base{};
    std::size_t size{};
    DWORD protect{};
};

inline bool readable(DWORD protect) {
    return protect == PAGE_READONLY || protect == PAGE_READWRITE ||
           protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

inline std::vector<Region> regions(HANDLE process) {
    std::vector<Region> result;
    std::uint8_t* address = nullptr;
    MEMORY_BASIC_INFORMATION info{};
    for (;;) {
        if (!VirtualQueryEx(process, address, &info, sizeof(info)))
            break;
        auto* base = reinterpret_cast<std::uint8_t*>(info.BaseAddress);
        if (info.State == MEM_COMMIT && info.RegionSize != 0 && readable(info.Protect))
            result.push_back({reinterpret_cast<std::uintptr_t>(base),
                              static_cast<std::size_t>(info.RegionSize), info.Protect});
        auto* next = base + info.RegionSize;
        if (next <= address)
            break;
        address = next;
        if (result.size() > 200000)
            break;
    }
    return result;
}

inline bool contains(const std::vector<Region>& list, std::uintptr_t address) {
    auto it = std::upper_bound(list.begin(), list.end(), address,
        [](std::uintptr_t value, const Region& region) { return value < region.base; });
    if (it == list.begin())
        return false;
    --it;
    return address >= it->base && address - it->base < it->size;
}

inline bool read(HANDLE process, std::uintptr_t address, void* buffer, std::size_t size) {
    SIZE_T done = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &done) &&
           done == size;
}

inline bool write(HANDLE process, std::uintptr_t address, const void* buffer, std::size_t size) {
    SIZE_T done = 0;
    return WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), buffer, size, &done) &&
           done == size;
}

inline std::uint64_t load_le(const unsigned char* p, int width) {
    std::uint64_t value = 0;
    for (int i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    return value;
}

} // namespace shr
