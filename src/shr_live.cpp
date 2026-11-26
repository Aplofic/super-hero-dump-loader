// shr_live.dll — resolvedor Mono ao vivo, somente leitura.
// Escreve live_pointers.txt ao lado de Prototype.exe com ponteiros reais e
// offsets de classe resolvidos pela metadata do Mono (nao heap temporaria).
// Nao chama WriteProcessMemory dentro do jogo e nao suspende threads.
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct MonoDomain;
struct MonoAssembly;
struct MonoImage;
struct MonoClass;
struct MonoClassField;
struct MonoVTable;
struct MonoThread;
struct MonoObject;

using RootDomainFn = MonoDomain* (*)();
using AssemblyForeachFn = void (*)(void (*)(void*, void*), void*);
using AssemblyImageFn = MonoImage* (*)(MonoAssembly*);
using ImageNameFn = const char* (*)(MonoImage*);
using ClassFromNameFn = MonoClass* (*)(MonoImage*, const char*, const char*);
using ClassNameFn = const char* (*)(MonoClass*);
using ClassNamespaceFn = const char* (*)(MonoClass*);
using FieldFromNameFn = MonoClassField* (*)(MonoClass*, const char*);
using FieldNameFn = const char* (*)(MonoClassField*);
using FieldOffsetFn = int (*)(MonoClassField*);
using FieldFlagsFn = std::uint32_t (*)(MonoClassField*);
using ClassVtableFn = MonoVTable* (*)(MonoDomain*, MonoClass*);
using StaticDataFn = void* (*)(MonoVTable*);
using StaticValueFn = void (*)(MonoVTable*, MonoClassField*, void*);
using FieldValueFn = void (*)(MonoObject*, MonoClassField*, void*);
using AttachFn = MonoThread* (*)(MonoDomain*);
using DetachFn = void (*)(MonoThread*);

struct Api {
    HMODULE module{};
    RootDomainFn root_domain{};
    AssemblyForeachFn assembly_foreach{};
    AssemblyImageFn assembly_image{};
    ImageNameFn image_name{};
    ClassFromNameFn class_from_name{};
    ClassNameFn class_name{};
    ClassNamespaceFn class_namespace{};
    FieldFromNameFn field_from_name{};
    FieldNameFn field_name{};
    FieldOffsetFn field_offset{};
    FieldFlagsFn field_flags{};
    ClassVtableFn class_vtable{};
    StaticDataFn static_data{};
    StaticValueFn static_value{};
    FieldValueFn field_value{};
    AttachFn attach{};
    DetachFn detach{};
    bool complete{};
};

Api g_api;
MonoDomain* g_domain = nullptr;
std::vector<MonoImage*> g_images;

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
    g_api.assembly_foreach = symbol<AssemblyForeachFn>("mono_assembly_foreach");
    g_api.assembly_image = symbol<AssemblyImageFn>("mono_assembly_get_image");
    g_api.image_name = symbol<ImageNameFn>("mono_image_get_name");
    g_api.class_from_name = symbol<ClassFromNameFn>("mono_class_from_name");
    g_api.class_name = symbol<ClassNameFn>("mono_class_get_name");
    g_api.class_namespace = symbol<ClassNamespaceFn>("mono_class_get_namespace");
    g_api.field_from_name = symbol<FieldFromNameFn>("mono_class_get_field_from_name");
    g_api.field_name = symbol<FieldNameFn>("mono_field_get_name");
    g_api.field_offset = symbol<FieldOffsetFn>("mono_field_get_offset");
    g_api.field_flags = symbol<FieldFlagsFn>("mono_field_get_flags");
    g_api.class_vtable = symbol<ClassVtableFn>("mono_class_vtable");
    g_api.static_data = symbol<StaticDataFn>("mono_vtable_get_static_field_data");
    g_api.static_value = symbol<StaticValueFn>("mono_field_static_get_value");
    g_api.field_value = symbol<FieldValueFn>("mono_field_get_value");
    g_api.attach = symbol<AttachFn>("mono_thread_attach");
    g_api.detach = symbol<DetachFn>("mono_thread_detach");

    g_api.complete = g_api.root_domain && g_api.assembly_foreach && g_api.assembly_image &&
                     g_api.image_name && g_api.class_from_name && g_api.class_name &&
                     g_api.class_namespace && g_api.field_from_name && g_api.field_name &&
                     g_api.field_offset && g_api.field_flags && g_api.class_vtable &&
                     g_api.static_data && g_api.static_value && g_api.field_value &&
                     g_api.attach && g_api.detach;
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

bool safe_copy(std::uintptr_t address, void* destination, std::size_t size) {
    __try {
        std::memcpy(destination, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_qword(std::uintptr_t address, std::uint64_t& value) {
    return safe_copy(address, &value, sizeof(value));
}

bool safe_float(std::uintptr_t address, float& value) {
    return safe_copy(address, &value, sizeof(value));
}

bool safe_int32(std::uintptr_t address, std::int32_t& value) {
    return safe_copy(address, &value, sizeof(value));
}

void image_callback(void* data, void*) {
    if (!data)
        return;
    MonoImage* image = g_api.assembly_image(static_cast<MonoAssembly*>(data));
    if (image)
        g_images.push_back(image);
}

MonoImage* find_image(const char* name) {
    for (MonoImage* image : g_images) {
        const char* found = g_api.image_name(image);
        if (found && std::strcmp(found, name) == 0)
            return image;
    }
    return nullptr;
}

struct ClassHit {
    MonoImage* image{};
    MonoClass* klass{};
};

ClassHit find_class(const char* namespace_name, const char* class_name) {
    ClassHit hit;
    for (MonoImage* image : g_images) {
        MonoClass* klass = g_api.class_from_name(image, namespace_name, class_name);
        if (!klass)
            continue;
        const char* actual_name = g_api.class_name(klass);
        const char* actual_ns = g_api.class_namespace(klass);
        if (actual_name && std::strcmp(actual_name, class_name) == 0 &&
            ((actual_ns && namespace_name && std::strcmp(actual_ns, namespace_name) == 0) ||
             (!actual_ns && !namespace_name))) {
            hit = {image, klass};
            return hit;
        }
    }
    return hit;
}

MonoClassField* find_field(MonoClass* klass, const char* name) {
    if (!klass)
        return nullptr;
    MonoClassField* field = g_api.field_from_name(klass, name);
    if (!field)
        return nullptr;
    const char* actual = g_api.field_name(field);
    return actual && std::strcmp(actual, name) == 0 ? field : nullptr;
}

// __try cannot live in functions with C++ objects that need unwinding.
// Keep every guarded Mono call in tiny POD-only helpers.
MonoVTable* guarded_class_vtable(MonoClass* klass) {
    __try {
        return g_api.class_vtable(g_domain, klass);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* guarded_static_data(MonoVTable* vtable) {
    __try {
        return g_api.static_data(vtable);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool guarded_static_value(MonoVTable* vtable, MonoClassField* field, void* destination) {
    __try {
        g_api.static_value(vtable, field, destination);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_field_value(MonoObject* object, MonoClassField* field, void* destination) {
    __try {
        g_api.field_value(object, field, destination);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_foreach(void (*callback)(void*, void*)) {
    __try {
        g_api.assembly_foreach(callback, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

MonoThread* guarded_attach(MonoDomain* domain) {
    __try {
        return g_api.attach(domain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void guarded_detach(MonoThread* thread) {
    __try {
        g_api.detach(thread);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void report_static(std::wofstream& out, const char* namespace_name,
                   const char* class_name, const char* field_name) {
    ClassHit hit = find_class(namespace_name, class_name);
    out << L"STATIC " << from_utf8(namespace_name) << L"." << from_utf8(class_name)
        << L"::" << from_utf8(field_name);
    if (!hit.klass) {
        out << L" NOT_FOUND\n";
        return;
    }
    MonoClassField* field = find_field(hit.klass, field_name);
    if (!field) {
        out << L" FIELD_NOT_FOUND image=" << from_utf8(g_api.image_name(hit.image))
            << L"\n";
        return;
    }

    MonoVTable* vtable = guarded_class_vtable(hit.klass);
    void* static_base = vtable ? guarded_static_data(vtable) : nullptr;

    const int offset = g_api.field_offset(field);
    const std::uint32_t flags = g_api.field_flags(field);
    const std::uintptr_t address = static_base
        ? reinterpret_cast<std::uintptr_t>(static_base) + static_cast<std::uint32_t>(offset)
        : 0;
    std::uint64_t raw = 0;
    if (address)
        safe_qword(address, raw);

    std::uintptr_t resolved = 0;
    if (vtable && field)
        guarded_static_value(vtable, field, &resolved);

    out << L" image=" << from_utf8(g_api.image_name(hit.image))
        << L" field_offset=+0x" << hx(static_cast<std::uint32_t>(offset), 8)
        << L" flags=0x" << hx(flags, 8)
        << L" vtable=0x" << hx(reinterpret_cast<std::uintptr_t>(vtable), 16)
        << L" static_base=0x" << hx(reinterpret_cast<std::uintptr_t>(static_base), 16)
        << L" field_addr=0x" << hx(address, 16)
        << L" raw_qword=0x" << hx(raw, 16)
        << L" resolved_ptr=0x" << hx(resolved, 16)
        << L"\n";
}

bool get_static_pointer(MonoClass* klass, const char* field_name, std::uintptr_t& result) {
    result = 0;
    if (!klass || !g_domain)
        return false;
    MonoClassField* field = find_field(klass, field_name);
    if (!field)
        return false;
    MonoVTable* vtable = guarded_class_vtable(klass);
    if (!vtable)
        return false;
    guarded_static_value(vtable, field, &result);
    return result != 0;
}

bool get_instance_pointer(MonoObject* object, MonoClassField* field, std::uintptr_t& result) {
    result = 0;
    if (!object || !field)
        return false;
    __try {
        g_api.field_value(object, field, &result);
        return result != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool get_instance_float(MonoObject* object, MonoClassField* field, float& result) {
    result = 0.0f;
    if (!object || !field)
        return false;
    return guarded_field_value(object, field, &result);
}

void report_pointer_field(std::wofstream& out, const char* label, MonoObject* object,
                          MonoClassField* field) {
    const int offset = field ? g_api.field_offset(field) : 0;
    const std::uintptr_t address = object && field
        ? reinterpret_cast<std::uintptr_t>(object) + static_cast<std::uint32_t>(offset)
        : 0;
    std::uint64_t raw = 0;
    if (address)
        safe_qword(address, raw);

    std::uintptr_t value = 0;
    const bool resolved = get_instance_pointer(object, field, value);

    out << L"PTR " << from_utf8(label)
        << L" offset=+0x" << hx(static_cast<std::uint32_t>(offset), 8)
        << L" addr=0x" << hx(address, 16)
        << L" raw=0x" << hx(raw, 16)
        << L" value=0x" << hx(value, 16)
        << (resolved ? L" ok\n" : L" null_or_failed\n");
}

void report_float_field(std::wofstream& out, const char* label, MonoObject* object,
                        MonoClassField* field) {
    const int offset = field ? g_api.field_offset(field) : 0;
    const std::uintptr_t address = object && field
        ? reinterpret_cast<std::uintptr_t>(object) + static_cast<std::uint32_t>(offset)
        : 0;
    float raw = 0.0f;
    if (address)
        safe_float(address, raw);

    float value = 0.0f;
    const bool resolved = get_instance_float(object, field, value);

    out << L"FLT " << from_utf8(label)
        << L" offset=+0x" << hx(static_cast<std::uint32_t>(offset), 8)
        << L" addr=0x" << hx(address, 16)
        << L" raw=" << std::fixed << std::setprecision(3) << raw
        << L" value=" << value
        << (resolved ? L" ok\n" : L" failed\n");
}

void report_int_field(std::wofstream& out, const char* label, MonoObject* object,
                      MonoClassField* field) {
    const int offset = field ? g_api.field_offset(field) : 0;
    const std::uintptr_t address = object && field
        ? reinterpret_cast<std::uintptr_t>(object) + static_cast<std::uint32_t>(offset)
        : 0;
    std::int32_t raw = 0;
    if (address)
        safe_int32(address, raw);

    std::int32_t value = 0;
    if (object && field)
        guarded_field_value(object, field, &value);

    out << L"INT " << from_utf8(label)
        << L" offset=+0x" << hx(static_cast<std::uint32_t>(offset), 8)
        << L" addr=0x" << hx(address, 16)
        << L" raw=" << raw << L" value=" << value << L"\n";
}

std::wstring output_path() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring path(executable);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path.resize(slash);
    path += L"\\live_pointers.txt";
    return path;
}

DWORD WINAPI resolve_thread(LPVOID) {
    Sleep(600);
    std::wofstream out(output_path(), std::ios::trunc);
    if (!out) {
        OutputDebugStringW(L"shr_live: cannot open live_pointers.txt\n");
        return 1;
    }

    if (!load_api()) {
        out << L"ERROR mono exports missing\n";
        return 2;
    }

    g_domain = g_api.root_domain();
    if (!g_domain) {
        out << L"ERROR no domain\n";
        return 3;
    }

    MonoThread* thread = guarded_attach(g_domain);
    if (!guarded_foreach(image_callback)) {
        out << L"ERROR foreach access_violation\n";
        return 4;
    }

    out << L"=== SHR LIVE RESOLVER ===\n";
    out << L"mono_domain=0x" << hx(reinterpret_cast<std::uintptr_t>(g_domain), 16)
        << L" images=" << g_images.size() << L"\n\n";

    report_static(out, "", "JogadorBR", "<Local>k__BackingField");
    report_static(out, "", "MundoVivo", "_instancia");
    report_static(out, "", "MundoVivo2", "_inst");
    report_static(out, "Royale.Multiplayer", "SalaRede", "<Instancia>k__BackingField");
    report_static(out, "Royale.Multiplayer", "VidaRede", "<Local>k__BackingField");
    report_static(out, "", "Combatant", "All");
    out << L"\n";

    ClassHit jogador_hit = find_class("", "JogadorBR");
    if (!jogador_hit.klass) {
        out << L"ERROR JogadorBR class not found\n";
        if (thread)
            guarded_detach(thread);
        return 5;
    }

    std::uintptr_t local = 0;
    const bool local_ok = get_static_pointer(jogador_hit.klass, "<Local>k__BackingField", local);
    out << L"=== CADEIA LOCAL ===\n";
    out << L"JogadorBR::<Local> = 0x" << hx(local, 16)
        << (local_ok ? L" ok\n" : L" null_or_failed\n");

    if (local) {
        auto* local_object = reinterpret_cast<MonoObject*>(local);
        out << L"local_object_class="
            << from_utf8(g_api.class_name(g_api.class_from_name(jogador_hit.image, "", "JogadorBR")))
            << L"\n";

        report_pointer_field(out, "JogadorBR::combatant", local_object,
                             find_field(jogador_hit.klass, "combatant"));
        report_pointer_field(out, "JogadorBR::controller", local_object,
                             find_field(jogador_hit.klass, "controller"));
        report_pointer_field(out, "JogadorBR::hero", local_object,
                             find_field(jogador_hit.klass, "hero"));
        report_pointer_field(out, "JogadorBR::gun", local_object,
                             find_field(jogador_hit.klass, "gun"));
        report_pointer_field(out, "JogadorBR::inventory", local_object,
                             find_field(jogador_hit.klass, "inventory"));
        report_int_field(out, "JogadorBR::<IsDead>k__BackingField", local_object,
                         find_field(jogador_hit.klass, "<IsDead>k__BackingField"));

        std::uintptr_t combatant = 0;
        if (get_instance_pointer(local_object,
                                 find_field(jogador_hit.klass, "combatant"), combatant) &&
            combatant) {
            ClassHit combatant_hit = find_class("", "Combatant");
            auto* combatant_object = reinterpret_cast<MonoObject*>(combatant);
            out << L"\nCombatant object = 0x" << hx(combatant, 16) << L"\n";

            if (combatant_hit.klass) {
                report_int_field(out, "Combatant::isPlayer", combatant_object,
                                 find_field(combatant_hit.klass, "isPlayer"));
                report_float_field(out, "Combatant::maxHp", combatant_object,
                                   find_field(combatant_hit.klass, "maxHp"));
                report_int_field(out, "Combatant::vestLevel", combatant_object,
                                 find_field(combatant_hit.klass, "vestLevel"));
                report_float_field(out, "Combatant::vestDurability", combatant_object,
                                   find_field(combatant_hit.klass, "vestDurability"));
                report_int_field(out, "Combatant::helmetLevel", combatant_object,
                                 find_field(combatant_hit.klass, "helmetLevel"));
                report_float_field(out, "Combatant::helmetDurability", combatant_object,
                                   find_field(combatant_hit.klass, "helmetDurability"));
                report_float_field(out, "Combatant::<Hp>k__BackingField", combatant_object,
                                   find_field(combatant_hit.klass, "<Hp>k__BackingField"));
                report_int_field(out, "Combatant::<Kills>k__BackingField", combatant_object,
                                 find_field(combatant_hit.klass, "<Kills>k__BackingField"));
                report_int_field(out, "Combatant::<LastAttacker>k__BackingField", combatant_object,
                                 find_field(combatant_hit.klass, "<LastAttacker>k__BackingField"));
                report_float_field(out, "Combatant::<LastHpDamage>k__BackingField", combatant_object,
                                   find_field(combatant_hit.klass, "<LastHpDamage>k__BackingField"));
            } else {
                out << L"ERROR Combatant class not found\n";
            }
        } else {
            out << L"combatant pointer unavailable\n";
        }
    }

    out << L"\nDONE\n";
    out.flush();
    if (thread)
        guarded_detach(thread);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, resolve_thread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
