// shr_tool.exe — external reader for Super Hero Royale (x64, Unity Mono).
// Commands: pid, modules, regions, value, rescan, ptrmap, ptrpath, aob,
//           read, write, inject, clear, help, quit.
#define NOMINMAX
#include <Windows.h>
#include <tlhelp32.h>

#include "shr_common.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Hit {
    std::uintptr_t address{};
    std::uint64_t value{};
    int width{8};
};

struct Path {
    std::vector<std::uintptr_t> locations;
    std::vector<std::uintptr_t> offsets;
};

struct Module {
    std::wstring name;
    std::uintptr_t base{};
    std::size_t size{};
};

constexpr std::size_t kChunk = 1u << 16;
constexpr std::size_t kMaxHits = 200000;
constexpr std::size_t kMaxPaths = 20000;
constexpr std::size_t kMapLimit = 4000000;

std::vector<Hit> g_hits;
std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> g_pointer_map;
std::uint64_t g_pointer_count = 0;

std::string trim(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
        ++first;
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
        --last;
    return text.substr(first, last - first);
}

std::string rest(std::istringstream& stream) {
    std::string value;
    std::getline(stream, value);
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return trim(value);
}

std::wstring widen(const std::string& value) {
    if (value.empty())
        return {};
    int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
    return result;
}

std::string narrow_name(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t character : value)
        result.push_back(static_cast<char>(character & 0xFF));
    return result;
}

std::uint64_t hex_value(const std::string& text) {
    std::string value = trim(text);
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
        value = value.substr(2);
    if (value.empty())
        throw std::runtime_error("empty hex value");
    return std::stoull(value, nullptr, 16);
}

int int_value(const std::string& text, int fallback) {
    if (text.empty())
        return fallback;
    try {
        return std::stoi(text, nullptr, 0);
    } catch (...) {
        return fallback;
    }
}

DWORD find_pid(const std::string& name) {
    try {
        if (std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            return static_cast<DWORD>(std::stoul(name));
    } catch (...) {
        return 0;
    }

    const std::wstring wanted = widen(name);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD found = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, wanted.c_str()) == 0) {
                found = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

std::vector<Module> get_modules(DWORD pid) {
    std::vector<Module> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            result.push_back({entry.szModule, reinterpret_cast<std::uintptr_t>(entry.modBaseAddr),
                              entry.modBaseSize});
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    std::sort(result.begin(), result.end(), [](const Module& a, const Module& b) { return a.base < b.base; });
    return result;
}

std::string describe(std::uintptr_t address, const std::vector<Module>& modules) {
    for (const Module& module : modules) {
        if (address >= module.base && address - module.base < module.size) {
            std::ostringstream out;
            out << narrow_name(module.name) << "+0x" << std::hex << (address - module.base);
            return out.str();
        }
    }
    return "anon/heap";
}

bool in_module(std::uintptr_t address, const std::vector<Module>& modules) {
    for (const Module& module : modules)
        if (address >= module.base && address - module.base < module.size)
            return true;
    return false;
}

bool parse_pattern(const std::string& input, std::vector<std::uint8_t>& bytes, std::vector<bool>& exact) {
    bytes.clear();
    exact.clear();
    std::istringstream tokens(input);
    std::string token;
    while (tokens >> token) {
        token = trim(token);
        if (token == "??" || token == "?" || token == "*") {
            bytes.push_back(0);
            exact.push_back(false);
            continue;
        }
        if (token.size() != 2)
            return false;
        try {
            bytes.push_back(static_cast<std::uint8_t>(hex_value(token)));
        } catch (...) {
            return false;
        }
        exact.push_back(true);
    }
    return !bytes.empty() && bytes.size() <= 4096;
}

bool matches(const std::vector<std::uint8_t>& buffer, std::size_t offset,
             const std::vector<std::uint8_t>& pattern, const std::vector<bool>& exact) {
    if (offset + pattern.size() > buffer.size())
        return false;
    for (std::size_t i = 0; i < pattern.size(); ++i)
        if (exact[i] && buffer[offset + i] != pattern[i])
            return false;
    return true;
}

std::vector<Hit> scan_value(HANDLE process, std::uint64_t target, int width) {
    std::vector<Hit> hits;
    const std::vector<shr::Region> regions = shr::regions(process);
    std::vector<unsigned char> buffer(kChunk);
    for (const shr::Region& region : regions) {
        for (std::size_t offset = 0; offset < region.size; offset += kChunk) {
            const std::size_t amount = std::min(kChunk, region.size - offset);
            const std::uintptr_t address = region.base + offset;
            if (!shr::read(process, address, buffer.data(), amount))
                continue;
            std::size_t start = static_cast<std::size_t>((width - (address % width)) % width);
            for (std::size_t i = start; i + width <= amount; i += static_cast<std::size_t>(width)) {
                const std::uint64_t value = shr::load_le(buffer.data() + i, width);
                if (value == target) {
                    hits.push_back({address + i, value, width});
                    if (hits.size() >= kMaxHits)
                        return hits;
                }
            }
        }
    }
    return hits;
}

bool build_pointer_map(HANDLE process, std::size_t limit) {
    g_pointer_map.clear();
    g_pointer_count = 0;
    const std::vector<shr::Region> regions = shr::regions(process);
    g_pointer_map.reserve(std::min<std::size_t>(limit, 1000000));
    std::vector<unsigned char> buffer(kChunk);
    for (const shr::Region& region : regions) {
        for (std::size_t offset = 0; offset < region.size; offset += kChunk) {
            const std::size_t amount = std::min(kChunk, region.size - offset);
            const std::uintptr_t address = region.base + offset;
            if (!shr::read(process, address, buffer.data(), amount))
                continue;
            const std::uintptr_t aligned = (address + 7u) & ~std::uintptr_t{7u};
            std::size_t i = static_cast<std::size_t>(aligned - address);
            for (; i + 8 <= amount; i += 8) {
                const std::uint64_t value = shr::load_le(buffer.data() + i, 8);
                if (!shr::contains(regions, static_cast<std::uintptr_t>(value)))
                    continue;
                auto& slots = g_pointer_map[static_cast<std::uintptr_t>(value)];
                if (slots.size() < 4096)
                    slots.push_back(address + i);
                if (++g_pointer_count >= limit)
                    return true;
            }
        }
    }
    return true;
}

std::vector<Path> build_paths(std::uintptr_t target, int depth, std::uintptr_t max_offset) {
    std::vector<Path> paths;
    auto direct = g_pointer_map.find(target);
    if (direct == g_pointer_map.end())
        return paths;
    for (std::uintptr_t location : direct->second)
        paths.push_back({{location}, {0}});

    for (int level = 1; level < depth; ++level) {
        std::vector<Path> next;
        for (const Path& path : paths) {
            const std::uintptr_t node = path.locations.back();
            for (std::uintptr_t offset = 0; offset <= max_offset; offset += 8) {
                auto found = g_pointer_map.find(node - offset);
                if (found == g_pointer_map.end())
                    continue;
                for (std::uintptr_t location : found->second) {
                    Path candidate = path;
                    candidate.locations.push_back(location);
                    candidate.offsets.push_back(offset);
                    next.push_back(std::move(candidate));
                    if (next.size() >= kMaxPaths)
                        return next;
                }
            }
        }
        paths.swap(next);
        if (paths.empty())
            break;
    }
    return paths;
}

// Module images are usually scanned last, so a global pointer map limited to a
// few million entries never reaches them. Add only module-resident pointers as
// additional roots; then a path can terminate at "Module.dll+0xOFFSET".
void add_module_pointer_roots(HANDLE process, const std::vector<Module>& modules,
                              const std::vector<shr::Region>& regions) {
    std::vector<unsigned char> buffer(kChunk);
    for (const shr::Region& region : regions) {
        bool module_region = false;
        for (const Module& module : modules) {
            if (region.base >= module.base && region.base - module.base < module.size) {
                module_region = true;
                break;
            }
        }
        if (!module_region)
            continue;

        for (std::size_t offset = 0; offset < region.size; offset += kChunk) {
            const std::size_t amount = std::min(kChunk, region.size - offset);
            const std::uintptr_t address = region.base + offset;
            if (!shr::read(process, address, buffer.data(), amount))
                continue;
            const std::uintptr_t aligned = (address + 7u) & ~std::uintptr_t{7u};
            std::size_t i = static_cast<std::size_t>(aligned - address);
            for (; i + 8 <= amount; i += 8) {
                const std::uint64_t value = shr::load_le(buffer.data() + i, 8);
                if (!value || !shr::contains(regions, static_cast<std::uintptr_t>(value)))
                    continue;
                auto& slots = g_pointer_map[static_cast<std::uintptr_t>(value)];
                if (slots.size() < 4096)
                    slots.push_back(address + i);
            }
        }
    }
}

std::vector<Path> build_root_paths(std::uintptr_t target, int depth,
                                   std::uintptr_t max_offset,
                                   const std::vector<Module>& modules) {
    std::vector<Path> paths;
    auto direct = g_pointer_map.find(target);
    if (direct == g_pointer_map.end())
        return paths;
    for (std::uintptr_t location : direct->second)
        paths.push_back({{location}, {0}});

    std::vector<Path> roots;
    for (int level = 0; level <= depth; ++level) {
        std::vector<Path> next;
        for (const Path& path : paths) {
            const std::uintptr_t node = path.locations.back();
            if (in_module(node, modules)) {
                roots.push_back(path);
                if (roots.size() >= 200)
                    return roots;
                continue;
            }
            if (level == depth)
                continue;
            for (std::uintptr_t offset = 0; offset <= max_offset; offset += 8) {
                auto found = g_pointer_map.find(node - offset);
                if (found == g_pointer_map.end())
                    continue;
                for (std::uintptr_t location : found->second) {
                    Path candidate = path;
                    candidate.locations.push_back(location);
                    candidate.offsets.push_back(offset);
                    next.push_back(std::move(candidate));
                    if (next.size() >= kMaxPaths)
                        break;
                }
                if (next.size() >= kMaxPaths)
                    break;
            }
            if (next.size() >= kMaxPaths)
                break;
        }
        paths.swap(next);
        if (paths.empty())
            break;
    }
    return roots;
}

bool inject(DWORD pid, const std::string& dll_path) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                 PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) {
        std::cout << "OpenProcess failed: " << GetLastError() << "\n";
        return false;
    }
    const std::wstring path = widen(dll_path);
    const std::size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    LPVOID memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) {
        std::cout << "VirtualAllocEx failed: " << GetLastError() << "\n";
        CloseHandle(process);
        return false;
    }
    if (!WriteProcessMemory(process, memory, path.data(), bytes, nullptr)) {
        std::cout << "WriteProcessMemory failed: " << GetLastError() << "\n";
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    FARPROC loader = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loader) {
        std::cout << "LoadLibraryW lookup failed\n";
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loader), memory, 0, nullptr);
    if (!thread) {
        std::cout << "CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    WaitForSingleObject(thread, 15000);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    std::cout << "LoadLibraryW return: 0x" << std::hex << code << std::dec << "\n";
    CloseHandle(thread);
    VirtualFreeEx(process, memory, 0, MEM_RELEASE);
    CloseHandle(process);
    return code != 0;
}

void print_help() {
    std::cout <<
        "pid <name|pid>\n"
        "modules <pid>\n"
        "regions <pid>\n"
        "value <pid> <hex> [4|8]        find addresses holding value\n"
        "rescan <pid> <hex> [4|8]       narrow previous hits\n"
        "ptrmap <pid> [limit]           map pointer-shaped qwords\n"
        "ptrpath <pid> <addr> [depth] [maxOffset]\n"
        "rootpath <pid> <addr> [depth] [maxOffset]  path anchored in a module image\n"
        "aob <pid> \"AA ?? BB ...\"       signature scan\n"
        "modfind <pid> \"AA ?? BB ...\"   signature scan limited to module images\n"
        "read <pid> <hex> [bytes]\n"
        "write <pid> <hex> <hexbytes>\n"
        "inject <pid> <dll>\n"
        "clear | help | quit\n";
}

} // namespace

int main() {
    std::cout << "shr_tool — external mono/pointer bench, x64\n";
    std::string line;
    while (true) {
        std::cout << "shr> ";
        if (!std::getline(std::cin, line))
            break;
        std::istringstream input(line);
        std::string command;
        if (!(input >> command))
            continue;

        try {
            if (command == "quit" || command == "exit")
                break;

            if (command == "help") {
                print_help();
                continue;
            }

            if (command == "clear") {
                g_hits.clear();
                g_pointer_map.clear();
                g_pointer_count = 0;
                std::cout << "state cleared\n";
                continue;
            }

            if (command == "pid") {
                std::string name;
                input >> name;
                std::cout << "pid=" << find_pid(name) << "\n";
                continue;
            }

            DWORD pid = 0;
            if (command != "pid") {
                std::string pid_text;
                input >> pid_text;
                pid = find_pid(pid_text);
                if (!pid) {
                    std::cout << "bad pid\n";
                    continue;
                }
            }

            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!process && command != "inject") {
                std::cout << "OpenProcess failed: " << GetLastError() << "\n";
                continue;
            }

            if (command == "modules") {
                const std::vector<Module> modules = get_modules(pid);
                std::cout << modules.size() << " modules\n";
                for (std::size_t i = 0; i < modules.size() && i < 200; ++i) {
                    std::cout << std::hex << std::setw(16) << std::setfill('0') << modules[i].base
                              << std::dec << "  " << std::setw(9) << modules[i].size << "  "
                              << narrow_name(modules[i].name) << "\n";
                }
            } else if (command == "regions") {
                const std::vector<Module> modules = get_modules(pid);
                const std::vector<shr::Region> list = shr::regions(process);
                std::cout << list.size() << " readable regions\n";
                for (std::size_t i = 0; i < list.size() && i < 200; ++i) {
                    std::cout << std::hex << std::setw(16) << std::setfill('0') << list[i].base
                              << "  " << std::setw(10) << list[i].size << std::dec
                              << "  protect=0x" << std::hex << list[i].protect << std::dec
                              << "  " << describe(list[i].base, modules) << "\n";
                }
            } else if (command == "value") {
                std::string target_text, width_text;
                input >> target_text >> width_text;
                const int width = int_value(width_text, 8);
                g_hits = scan_value(process, hex_value(target_text), width == 4 ? 4 : 8);
                const std::vector<Module> modules = get_modules(pid);
                std::cout << g_hits.size() << " hits\n";
                for (std::size_t i = 0; i < g_hits.size() && i < 40; ++i) {
                    std::cout << std::hex << std::setw(16) << std::setfill('0') << g_hits[i].address
                              << std::dec << "  " << describe(g_hits[i].address, modules) << "\n";
                }
            } else if (command == "rescan") {
                std::string target_text, width_text;
                input >> target_text >> width_text;
                const std::uint64_t target = hex_value(target_text);
                if (!width_text.empty()) {
                    const int width = int_value(width_text, 8);
                    for (Hit& hit : g_hits)
                        hit.width = width == 4 ? 4 : 8;
                }
                std::vector<Hit> kept;
                for (const Hit& hit : g_hits) {
                    unsigned char raw[8]{};
                    if (shr::read(process, hit.address, raw, static_cast<std::size_t>(hit.width)) &&
                        shr::load_le(raw, hit.width) == target)
                        kept.push_back(hit);
                }
                g_hits.swap(kept);
                std::cout << g_hits.size() << " hits remain\n";
            } else if (command == "ptrmap") {
                std::string limit_text;
                input >> limit_text;
                const std::size_t limit = limit_text.empty() ? kMapLimit :
                    static_cast<std::size_t>(hex_value(limit_text));
                build_pointer_map(process, limit);
                std::cout << g_pointer_count << " pointers, " << g_pointer_map.size() << " targets\n";
            } else if (command == "ptrpath") {
                std::string target_text, depth_text, offset_text;
                input >> target_text >> depth_text >> offset_text;
                if (g_pointer_map.empty()) {
                    std::cout << "ptrmap first\n";
                } else {
                    const std::uintptr_t target = static_cast<std::uintptr_t>(hex_value(target_text));
                    const int depth = std::clamp(int_value(depth_text, 4), 1, 12);
                    const std::uintptr_t max_offset =
                        static_cast<std::uintptr_t>(hex_value(offset_text.empty() ? "1000" : offset_text));
                    const std::vector<Path> paths = build_paths(target, depth, max_offset);
                    const std::vector<Module> modules = get_modules(pid);
                    std::cout << paths.size() << " paths\n";
                    for (std::size_t n = 0; n < paths.size() && n < 50; ++n) {
                        const Path& path = paths[n];
                        std::cout << "[" << n << "] ";
                        for (int i = static_cast<int>(path.locations.size()) - 1; i >= 0; --i) {
                            std::cout << std::hex << std::setw(16) << std::setfill('0')
                                      << path.locations[static_cast<std::size_t>(i)] << std::dec
                                      << " " << describe(path.locations[static_cast<std::size_t>(i)], modules)
                                      << " -0x" << std::hex << path.offsets[static_cast<std::size_t>(i)]
                                      << " -> ";
                        }
                        std::cout << std::hex << std::setw(16) << std::setfill('0') << target
                                  << std::dec << " " << describe(target, modules) << "\n";
                    }
                }
            } else if (command == "rootpath") {
                std::string target_text, depth_text, offset_text;
                input >> target_text >> depth_text >> offset_text;
                if (g_pointer_map.empty()) {
                    std::cout << "ptrmap first\n";
                } else {
                    const std::uintptr_t target = static_cast<std::uintptr_t>(hex_value(target_text));
                    const int depth = std::clamp(int_value(depth_text, 6), 1, 12);
                    const std::uintptr_t max_offset =
                        static_cast<std::uintptr_t>(hex_value(offset_text.empty() ? "1000" : offset_text));
                    const std::vector<Module> modules = get_modules(pid);
                    add_module_pointer_roots(process, modules, shr::regions(process));
                    const std::vector<Path> paths = build_root_paths(target, depth, max_offset, modules);
                    std::cout << paths.size() << " module-rooted paths\n";
                    for (std::size_t n = 0; n < paths.size() && n < 50; ++n) {
                        const Path& path = paths[n];
                        std::cout << "[" << n << "] ";
                        for (int i = static_cast<int>(path.locations.size()) - 1; i >= 0; --i) {
                            std::cout << std::hex << std::setw(16) << std::setfill('0')
                                      << path.locations[static_cast<std::size_t>(i)] << std::dec
                                      << " " << describe(path.locations[static_cast<std::size_t>(i)], modules)
                                      << " -0x" << std::hex << path.offsets[static_cast<std::size_t>(i)]
                                      << " -> ";
                        }
                        std::cout << std::hex << std::setw(16) << std::setfill('0') << target
                                  << std::dec << " " << describe(target, modules) << "\n";
                    }
                }
            } else if (command == "aob") {
                const std::string pattern_text = rest(input);
                std::vector<std::uint8_t> pattern;
                std::vector<bool> exact;
                if (!parse_pattern(pattern_text, pattern, exact)) {
                    std::cout << "bad pattern\n";
                    continue;
                }
                const std::vector<Module> modules = get_modules(pid);
                const std::vector<shr::Region> list = shr::regions(process);
                std::vector<unsigned char> buffer(kChunk);
                std::size_t printed = 0;
                for (const shr::Region& region : list) {
                    if (printed >= 256)
                        break;
                    for (std::size_t offset = 0; offset < region.size; offset += kChunk) {
                        const std::size_t amount = std::min(kChunk, region.size - offset);
                        const std::uintptr_t address = region.base + offset;
                        if (!shr::read(process, address, buffer.data(), amount))
                            continue;
                        for (std::size_t i = 0; i + pattern.size() <= amount; ++i) {
                            if (!matches(buffer, i, pattern, exact))
                                continue;
                            std::cout << std::hex << std::setw(16) << std::setfill('0') << (address + i)
                                      << std::dec << "  " << describe(address + i, modules) << "\n";
                            if (++printed >= 256)
                                break;
                        }
                    }
                }
                std::cout << printed << " matches shown\n";
            } else if (command == "modfind") {
                const std::string pattern_text = rest(input);
                std::vector<std::uint8_t> pattern;
                std::vector<bool> exact;
                if (!parse_pattern(pattern_text, pattern, exact)) {
                    std::cout << "bad pattern\n";
                    continue;
                }
                const std::vector<Module> modules = get_modules(pid);
                std::vector<unsigned char> buffer(kChunk);
                std::size_t printed = 0;
                for (const Module& module : modules) {
                    for (std::size_t offset = 0; offset < module.size; offset += kChunk) {
                        const std::size_t amount = std::min(kChunk, module.size - offset);
                        const std::uintptr_t address = module.base + offset;
                        if (!shr::read(process, address, buffer.data(), amount))
                            continue;
                        for (std::size_t i = 0; i + pattern.size() <= amount; ++i) {
                            if (!matches(buffer, i, pattern, exact))
                                continue;
                            std::cout << std::hex << std::setw(16) << std::setfill('0') << (address + i)
                                      << std::dec << "  " << describe(address + i, modules) << "\n";
                            if (++printed >= 256)
                                break;
                        }
                        if (printed >= 256)
                            break;
                    }
                    if (printed >= 256)
                        break;
                }
                std::cout << printed << " module matches shown\n";
            } else if (command == "read") {
                std::string address_text, size_text;
                input >> address_text >> size_text;
                const std::uintptr_t address = static_cast<std::uintptr_t>(hex_value(address_text));
                const std::size_t size = static_cast<std::size_t>(
                    std::clamp(int_value(size_text, 128), 0, 4096));
                std::vector<unsigned char> buffer(size);
                if (!shr::read(process, address, buffer.data(), size)) {
                    std::cout << "read failed\n";
                } else {
                    for (std::size_t i = 0; i < size; i += 16) {
                        std::cout << std::hex << std::setw(16) << std::setfill('0') << (address + i) << "  ";
                        for (std::size_t j = 0; j < 16; ++j)
                            std::cout << (i + j < size ? [&] { std::ostringstream s;
                                s << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(buffer[i + j]);
                                return s.str(); }() : std::string("  ")) << ' ';
                        std::cout << " ";
                        for (std::size_t j = 0; j < 16 && i + j < size; ++j) {
                            unsigned char c = buffer[i + j];
                            std::cout << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
                        }
                        std::cout << std::dec << "\n";
                    }
                }
            } else if (command == "write") {
                std::string address_text = rest(input);
                std::istringstream parts(address_text);
                std::string hex_bytes;
                parts >> address_text >> hex_bytes;
                const std::uintptr_t address = static_cast<std::uintptr_t>(hex_value(address_text));
                std::vector<unsigned char> data;
                for (std::size_t i = 0; i + 1 < hex_bytes.size(); i += 2)
                    data.push_back(static_cast<unsigned char>(hex_value(hex_bytes.substr(i, 2))));
                if (data.empty() || data.size() > 4096) {
                    std::cout << "bad byte count\n";
                    continue;
                }
                const bool ok = shr::write(process, address, data.data(), data.size());
                std::cout << (ok ? "write ok\n" : "write failed\n");
            } else if (command == "inject") {
                std::string dll_path = rest(input);
                CloseHandle(process);
                process = nullptr;
                inject(pid, dll_path);
            } else {
                std::cout << "unknown command\n";
            }

            if (process)
                CloseHandle(process);
        } catch (const std::exception& error) {
            std::cout << "error: " << error.what() << "\n";
        }
    }
    return 0;
}
