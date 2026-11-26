// shr_dump.dll — injected into the running Mono game; writes offsets_dump.txt
// next to Prototype.exe. Uses only exports verified present in the shipped
// mono-2.0-bdwgc.dll: no mono_domain_get_assemblies in this build.
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

constexpr bool kWantAllAssemblies = false;
constexpr std::uint32_t kFieldBudget = 500000;
constexpr std::uint32_t kTableTypeDef = 0x02;
constexpr std::uint32_t kFieldAttributeStatic = 0x0010;
constexpr std::uint32_t kFieldAttributeLiteral = 0x0040;

struct MonoDomain;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoClassField;
struct MonoVTable;
struct MonoThread;

using RootDomainFn = MonoDomain* (*)();
using DomainGetFn = MonoDomain* (*)();
using DomainAssemblyOpenFn = MonoAssembly* (*)(MonoDomain*, const char*);
using AssemblyForeachFn = void (*)(void (*)(void*, void*), void*);
using AssemblyImageFn = MonoImage* (*)(MonoAssembly*);
using ImageNameFn = const char* (*)(MonoImage*);
using TableRowsFn = std::uint32_t (*)(MonoImage*, int);
using ClassGetFn = MonoClass* (*)(MonoImage*, std::uint32_t);
using NameFn = const char* (*)(MonoClass*);
using NamespaceFn = const char* (*)(MonoClass*);
using FieldsFn = MonoClassField* (*)(MonoClass*, void**);
using NestedFn = MonoClass** (*)(MonoClass*, std::uint32_t*);
using FieldNameFn = const char* (*)(MonoClassField*);
using FieldOffsetFn = int (*)(MonoClassField*);
using FieldFlagsFn = std::uint32_t (*)(MonoClassField*);
using ClassVtableFn = MonoVTable* (*)(MonoDomain*, MonoClass*);
using StaticDataFn = void* (*)(MonoVTable*);
using StaticValueFn = void (*)(MonoVTable*, MonoClassField*, void*);
using AttachFn = MonoThread* (*)(MonoDomain*);
using DetachFn = void (*)(MonoThread*);

struct Api {
    HMODULE module{};
    RootDomainFn root_domain{};
    DomainGetFn domain_get{};
    DomainAssemblyOpenFn assembly_open{};
    AssemblyForeachFn assembly_foreach{};
    AssemblyImageFn assembly_image{};
    ImageNameFn image_name{};
    TableRowsFn table_rows{};
    ClassGetFn class_get{};
    NameFn class_name{};
    NamespaceFn class_namespace{};
    FieldsFn class_fields{};
    NestedFn nested_types{};
    FieldNameFn field_name{};
    FieldOffsetFn field_offset{};
    FieldFlagsFn field_flags{};
    ClassVtableFn class_vtable{};
    StaticDataFn static_data{};
    StaticValueFn static_value{};
    AttachFn attach{};
    DetachFn detach{};
    bool complete{};
};

Api g_api;

template <typename T>
T symbol(const char* name) {
    return reinterpret_cast<T>(GetProcAddress(g_api.module, name));
}

bool load_api() {
    g_api.module = GetModuleHandleW(L"mono-2.0-bdwgc.dll");
    if (!g_api.module)
        g_api.module = LoadLibraryW(L"mono-2.0-bdwgc.dll");
    if (!g_api.module)
        return false;
    g_api.root_domain = symbol<RootDomainFn>("mono_get_root_domain");
    g_api.domain_get = symbol<DomainGetFn>("mono_domain_get");
    g_api.assembly_open = symbol<DomainAssemblyOpenFn>("mono_domain_assembly_open");
    g_api.assembly_foreach = symbol<AssemblyForeachFn>("mono_assembly_foreach");
    g_api.assembly_image = symbol<AssemblyImageFn>("mono_assembly_get_image");
    g_api.image_name = symbol<ImageNameFn>("mono_image_get_name");
    g_api.table_rows = symbol<TableRowsFn>("mono_image_get_table_rows");
    g_api.class_get = symbol<ClassGetFn>("mono_class_get");
    g_api.class_name = symbol<NameFn>("mono_class_get_name");
    g_api.class_namespace = symbol<NamespaceFn>("mono_class_get_namespace");
    g_api.class_fields = symbol<FieldsFn>("mono_class_get_fields");
    g_api.nested_types = symbol<NestedFn>("mono_class_get_nested_types");
    g_api.field_name = symbol<FieldNameFn>("mono_field_get_name");
    g_api.field_offset = symbol<FieldOffsetFn>("mono_field_get_offset");
    g_api.field_flags = symbol<FieldFlagsFn>("mono_field_get_flags");
    g_api.class_vtable = symbol<ClassVtableFn>("mono_class_vtable");
    g_api.static_data = symbol<StaticDataFn>("mono_vtable_get_static_field_data");
    g_api.static_value = symbol<StaticValueFn>("mono_field_static_get_value");
    g_api.attach = symbol<AttachFn>("mono_thread_attach");
    g_api.detach = symbol<DetachFn>("mono_thread_detach");
    g_api.complete = g_api.root_domain && g_api.assembly_open && g_api.assembly_foreach && g_api.assembly_image &&
                     g_api.image_name && g_api.table_rows && g_api.class_get &&
                     g_api.class_name && g_api.class_namespace && g_api.class_fields &&
                     g_api.nested_types && g_api.field_name && g_api.field_offset &&
                     g_api.field_flags && g_api.class_vtable && g_api.static_data &&
                     g_api.static_value && g_api.attach && g_api.detach;
    return g_api.complete;
}

std::wstring from_utf8(const char* text) {
    if (!text)
        return L"?";
    int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0)
        return L"?";
    std::wstring result(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    return result;
}

template <typename T>
std::wstring hx(T value, int digits) {
    std::wostringstream out;
    out << std::hex << std::uppercase << std::setw(digits) << std::setfill(L'0')
        << static_cast<std::uint64_t>(value);
    return out.str();
}

bool safe_qword(std::uintptr_t address, std::uint64_t& value) {
    __try {
        value = *reinterpret_cast<const std::uint64_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool wanted(const std::wstring& assembly) {
    if (kWantAllAssemblies)
        return true;
    static const wchar_t* prefixes[] = {
        L"Assembly-CSharp", L"Royale.", L"FishNet", L"UnityEngine.CoreModule",
        L"UnityEngine", L"Unity.InputSystem", L"Unity.Scripting"
    };
    for (const wchar_t* prefix : prefixes)
        if (assembly.rfind(prefix, 0) == 0)
            return true;
    return false;
}

std::wstring output_path() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring path(executable);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path.resize(slash);
    path += L"\\offsets_dump.txt";
    return path;
}

bool guarded_dump_class(MonoDomain* domain, MonoClass* klass, std::wofstream* output,
                        int depth, std::uint32_t* budget);

void dump_class(MonoDomain* domain, MonoClass* klass, std::wofstream& out,
                int depth, std::uint32_t& budget) {
    if (!klass || depth > 5 || budget == 0)
        return;
    const char* raw_namespace = g_api.class_namespace(klass);
    const char* raw_name = g_api.class_name(klass);
    if (!raw_name)
        return;
    const std::wstring full = from_utf8(raw_namespace) + L"." + from_utf8(raw_name);
    out << L"CLASS " << full << L"\n";

    MonoVTable* vtable = nullptr;
    void* static_base = nullptr;
    bool vtable_ready = false;

    void* iterator = nullptr;
    while (budget != 0) {
        MonoClassField* field = g_api.class_fields(klass, &iterator);
        if (!field)
            break;
        --budget;
        const std::uint32_t pre_flags = g_api.field_flags(field);
        if (pre_flags & kFieldAttributeLiteral)
            continue;
        const char* raw_field = g_api.field_name(field);
        const int offset = g_api.field_offset(field);
        const std::uint32_t flags = g_api.field_flags(field);
        const std::wstring field_name = from_utf8(raw_field ? raw_field : "?");
        const std::wstring flag_text = hx(flags, 8);

        if (flags & kFieldAttributeStatic) {
            if (!vtable_ready) {
                vtable = g_api.class_vtable(domain, klass);
                static_base = vtable ? g_api.static_data(vtable) : nullptr;
                vtable_ready = true;
            }
            if (static_base) {
                const std::uintptr_t address =
                    reinterpret_cast<std::uintptr_t>(static_base) + static_cast<std::uint32_t>(offset);
                std::uint64_t raw = 0;
                const bool readable = safe_qword(address, raw);
                out << L"FIELD " << full << L"::" << field_name
                    << L" STATIC addr=0x" << hx(address, 16)
                    << L" base=0x" << hx(reinterpret_cast<std::uintptr_t>(static_base), 16)
                    << L" +0x" << hx(static_cast<std::uint32_t>(offset), 8)
                    << L" flags=0x" << flag_text;
                if (readable)
                    out << L" qword=0x" << hx(raw, 16);
                out << L"\n";
            } else {
                out << L"FIELD " << full << L"::" << field_name
                    << L" STATIC_UNRESOLVED +0x" << hx(static_cast<std::uint32_t>(offset), 8)
                    << L" flags=0x" << flag_text << L"\n";
            }
        } else {
            out << L"FIELD " << full << L"::" << field_name
                << L" INSTANCE +0x" << hx(static_cast<std::uint32_t>(offset), 8)
                << L" flags=0x" << flag_text << L"\n";
        }
    }

    std::uint32_t nested_count = 0;
    MonoClass** nested = g_api.nested_types(klass, &nested_count);
    if (nested && nested_count <= 4096) {
        for (std::uint32_t i = 0; i < nested_count && budget != 0; ++i)
            guarded_dump_class(domain, nested[i], &out, depth + 1, &budget);
    }
}

bool guarded_dump_class(MonoDomain* domain, MonoClass* klass, std::wofstream* output,
                        int depth, std::uint32_t* budget) {
    __try {
        dump_class(domain, klass, *output, depth, *budget);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *output << L"SKIP_CLASS access_violation\n";
        return false;
    }
}

struct DumpContext {
    MonoDomain* domain{};
    std::wofstream* output{};
    std::uint32_t* budget{};
};

void assembly_callback(void* data, void* user_data) {
    if (!data || !user_data)
        return;
    auto* context = static_cast<DumpContext*>(user_data);
    if (!context->domain || !context->output || !context->budget || *context->budget == 0)
        return;
    auto* assembly = static_cast<MonoAssembly*>(data);
    MonoImage* image = g_api.assembly_image(assembly);
    if (!image)
        return;
    const char* raw_name = g_api.image_name(image);
    const std::wstring name = from_utf8(raw_name);
    if (!wanted(name))
        return;
    std::wofstream& out = *context->output;
    out << L"ASSEMBLY " << name << L"\n";
    const std::uint32_t rows = g_api.table_rows(image, static_cast<int>(kTableTypeDef));
    const std::uint32_t limit = rows < 200000 ? rows : 200000;
    for (std::uint32_t token = 1; token <= limit && *context->budget != 0; ++token) {
        MonoClass* klass = g_api.class_get(image, (kTableTypeDef << 24) | token);
        if (klass)
            guarded_dump_class(context->domain, klass, &out, 0, context->budget);
    }
}

void guarded_assembly_callback(void* data, void* user_data) {
    __try {
        assembly_callback(data, user_data);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        auto* context = static_cast<DumpContext*>(user_data);
        if (context && context->output)
            *context->output << L"SKIP_ASSEMBLY access_violation\n";
    }
}

DWORD WINAPI dump_thread(LPVOID) {
    Sleep(1500);
    const std::wstring path = output_path();
    std::wofstream out(path, std::ios::trunc);
    if (!out) {
        OutputDebugStringW(L"shr_dump: cannot open offsets_dump.txt\n");
        return 1;
    }

    if (!load_api()) {
        out << L"ERROR mono exports missing\n";
        return 2;
    }

    MonoDomain* domain = g_api.root_domain();
    if (!domain && g_api.domain_get)
        domain = g_api.domain_get();
    if (!domain) {
        out << L"ERROR no domain\n";
        return 3;
    }
    MonoThread* thread = g_api.attach(domain);

    std::uint32_t budget = kFieldBudget;
    DumpContext context{domain, &out, &budget};
    g_api.assembly_foreach(guarded_assembly_callback, &context);

    out << L"DONE field_budget_left=" << budget << L"\n";
    out.flush();
    if (thread)
        g_api.detach(thread);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, dump_thread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
