// shr_cheat_probe.dll â€” somente leitura: confirma tipos/ponteiros para HP, combustÃ­vel e ossos dos bots.
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
struct MonoDomain; struct MonoAssembly; struct MonoImage; struct MonoClass;
struct MonoClassField; struct MonoType; struct MonoProperty; struct MonoMethod;
struct MonoVTable; struct MonoThread; struct MonoObject; struct MonoArray; struct MonoString;

using RootDomainFn = MonoDomain*(*)();
using AssemblyForeachFn = void(*)(void(*)(void*, void*), void*);
using AssemblyImageFn = MonoImage*(*)(MonoAssembly*);
using ClassFromNameFn = MonoClass*(*)(MonoImage*, const char*, const char*);
using ClassNameFn = const char*(*)(MonoClass*);
using ClassParentFn = MonoClass*(*)(MonoClass*);
using ClassGetFieldsFn = MonoClassField*(*)(MonoClass*, void**);
using FieldNameFn = const char*(*)(MonoClassField*);
using FieldOffsetFn = int(*)(MonoClassField*);
using FieldTypeFn = MonoType*(*)(MonoClassField*);
using TypeNameFn = const char*(*)(MonoType*);
using FieldValueFn = void(*)(MonoObject*, MonoClassField*, void*);
using ClassGetPropertiesFn = MonoProperty*(*)(MonoClass*, void**);
using PropertyGetNameFn = const char*(*)(MonoProperty*);
using PropertyGetValueFn = MonoObject*(*)(MonoProperty*, MonoObject*, void**, MonoObject**);
using ClassGetMethodsFn = MonoMethod*(*)(MonoClass*, void**);
using MethodGetNameFn = const char*(*)(MonoMethod*);
using RuntimeInvokeFn = MonoObject*(*)(MonoMethod*, MonoObject*, void**, MonoObject**);
using ObjectGetClassFn = MonoClass*(*)(MonoObject*);
using ClassVtableFn = MonoVTable*(*)(MonoDomain*, MonoClass*);
using StaticValueFn = void(*)(MonoVTable*, MonoClassField*, void*);
using AttachFn = MonoThread*(*)(MonoDomain*);
using DetachFn = void(*)(MonoThread*);
using ArrayLengthFn = std::uintptr_t(*)(MonoArray*);
using ArrayAddrFn = void*(*)(MonoArray*, int, std::uintptr_t);
using ClassGetFn = MonoClass*(*)(MonoImage*, std::uint32_t);
using TableRowsFn = int(*)(MonoImage*, int);
using ClassNamespaceFn = const char*(*)(MonoClass*);

struct Api {
    HMODULE module{};
    RootDomainFn root{}; AssemblyForeachFn foreach{}; AssemblyImageFn image{};
    ClassFromNameFn from_name{}; ClassNameFn class_name{}; ClassParentFn parent{};
    ClassGetFieldsFn fields{}; FieldNameFn field_name{}; FieldOffsetFn field_offset{};
    FieldTypeFn field_type{}; TypeNameFn type_name{}; FieldValueFn field_value{};
    ClassGetPropertiesFn props{}; PropertyGetNameFn prop_name{}; PropertyGetValueFn prop_value{};
    ClassGetMethodsFn methods{}; MethodGetNameFn method_name{}; RuntimeInvokeFn invoke{};
    ObjectGetClassFn object_class{}; ClassVtableFn vtable{}; StaticValueFn static_value{};
    AttachFn attach{}; DetachFn detach{}; ArrayLengthFn array_length{}; ArrayAddrFn array_addr{};
    ClassGetFn class_get{}; TableRowsFn table_rows{}; ClassNamespaceFn class_namespace{};
} api;

MonoDomain* domain = nullptr;
std::vector<MonoImage*> images;

template<typename T> T sym(const char* n) { return reinterpret_cast<T>(GetProcAddress(api.module, n)); }
bool load() {
    api.module = GetModuleHandleW(L"mono-2.0-bdwgc.dll");
    if (!api.module) return false;
    api.root = sym<RootDomainFn>("mono_get_root_domain");
    api.foreach = sym<AssemblyForeachFn>("mono_assembly_foreach");
    api.image = sym<AssemblyImageFn>("mono_assembly_get_image");
    api.from_name = sym<ClassFromNameFn>("mono_class_from_name");
    api.class_name = sym<ClassNameFn>("mono_class_get_name");
    api.parent = sym<ClassParentFn>("mono_class_get_parent");
    api.fields = sym<ClassGetFieldsFn>("mono_class_get_fields");
    api.field_name = sym<FieldNameFn>("mono_field_get_name");
    api.field_offset = sym<FieldOffsetFn>("mono_field_get_offset");
    api.field_type = sym<FieldTypeFn>("mono_field_get_type");
    api.type_name = sym<TypeNameFn>("mono_type_get_name");
    api.field_value = sym<FieldValueFn>("mono_field_get_value");
    api.props = sym<ClassGetPropertiesFn>("mono_class_get_properties");
    api.prop_name = sym<PropertyGetNameFn>("mono_property_get_name");
    api.prop_value = sym<PropertyGetValueFn>("mono_property_get_value");
    api.methods = sym<ClassGetMethodsFn>("mono_class_get_methods");
    api.method_name = sym<MethodGetNameFn>("mono_method_get_name");
    api.invoke = sym<RuntimeInvokeFn>("mono_runtime_invoke");
    api.object_class = sym<ObjectGetClassFn>("mono_object_get_class");
    api.vtable = sym<ClassVtableFn>("mono_class_vtable");
    api.static_value = sym<StaticValueFn>("mono_field_static_get_value");
    api.attach = sym<AttachFn>("mono_thread_attach");
    api.detach = sym<DetachFn>("mono_thread_detach");
    api.array_length = sym<ArrayLengthFn>("mono_array_length");
    api.array_addr = sym<ArrayAddrFn>("mono_array_addr_with_size");
    api.class_get = sym<ClassGetFn>("mono_class_get");
    api.table_rows = sym<TableRowsFn>("mono_image_get_table_rows");
    api.class_namespace = sym<ClassNamespaceFn>("mono_class_get_namespace");
    return api.root && api.foreach && api.from_name && api.field_type && api.type_name &&
           api.class_get && api.table_rows && api.class_namespace;
}

template<typename T> std::wstring hx(T v, int w) {
    std::wostringstream o; o << std::hex << std::uppercase << std::setw(w) << std::setfill(L'0') << (std::uint64_t)v; return o.str();
}
bool safe(std::uintptr_t a, void* d, std::size_t n) {
    __try { std::memcpy(d, (const void*)a, n); return true; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
MonoThread* att(MonoDomain* d) { __try { return api.attach(d); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void det(MonoThread* t) { __try { if (t) api.detach(t); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
void feach() { __try { api.foreach([](void* d, void*) { if (d) { MonoImage* i = api.image((MonoAssembly*)d); if (i) images.push_back(i); } }, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
MonoObject* prop(MonoProperty* p, MonoObject* o) { __try { return api.prop_value(p, o, nullptr, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
MonoObject* inv(MonoMethod* m, MonoObject* o, void** a) { __try { return api.invoke(m, o, a, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
MonoVTable* vt(MonoClass* c) { __try { return api.vtable(domain, c); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void stv(MonoVTable* v, MonoClassField* f, void* d) { __try { api.static_value(v, f, d); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
void fv(MonoObject* o, MonoClassField* f, void* d) { __try { api.field_value(o, f, d); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
MonoClass* goc(MonoObject* o) { __try { return api.object_class(o); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
const char* cname(MonoClass* c) { __try { return c ? api.class_name(c) : nullptr; } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
std::uintptr_t len(MonoArray* a) { __try { return api.array_length(a); } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
void* slot(MonoArray* a, std::uintptr_t i) { __try { return api.array_addr(a, 8, i); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
const char* tname(MonoClassField* f) { __try { MonoType* t = api.field_type(f); return t ? api.type_name(t) : nullptr; } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }

void cb(void* data, void*) { if (data) { MonoImage* i = api.image((MonoAssembly*)data); if (i) images.push_back(i); } }
MonoClass* find_class(const char* name) {
    for (MonoImage* image : images) {
        for (const char* ns : {"", "UnityEngine"}) {
            MonoClass* c = api.from_name(image, ns, name);
            if (!c) continue;
            const char* actual = api.class_name(c);
            if (actual && std::strcmp(actual, name) == 0) return c;
        }
    }
    return nullptr;
}
MonoClass* class_at(MonoImage* image, std::uint32_t token) { __try { return api.class_get(image, token); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
int rows(MonoImage* image, int table) { __try { return api.table_rows(image, table); } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
MonoClass* find_class_any(const char* name) {
    for (MonoImage* image : images) {
        const int count = rows(image, 0x02); // TypeDef
        if (count <= 0) continue;
        for (int i = 1; i <= count; ++i) {
            MonoClass* klass = class_at(image, 0x02000000u | static_cast<std::uint32_t>(i));
            if (!klass) continue;
            const char* actual = cname(klass);
            if (actual && std::strcmp(actual, name) == 0) return klass;
        }
    }
    return nullptr;
}
MonoClassField* find_field(MonoClass* c, const char* name) {
    for (MonoClass* k = c; k; k = api.parent(k)) {
        void* it = nullptr;
        while (true) {
            MonoClassField* f = api.fields(k, &it);
            if (!f) break;
            const char* n = api.field_name(f);
            if (n && std::strcmp(n, name) == 0) return f;
        }
    }
    return nullptr;
}
MonoProperty* find_prop(MonoClass* c, const char* name) {
    for (MonoClass* k = c; k; k = api.parent(k)) {
        void* it = nullptr;
        while (true) {
            MonoProperty* p = api.props(k, &it);
            if (!p) break;
            const char* n = api.prop_name(p);
            if (n && std::strcmp(n, name) == 0) return p;
        }
    }
    return nullptr;
}
MonoMethod* find_method(MonoClass* c, const char* name) {
    for (MonoClass* k = c; k; k = api.parent(k)) {
        void* it = nullptr;
        while (true) {
            MonoMethod* m = api.methods(k, &it);
            if (!m) break;
            const char* n = api.method_name(m);
            if (n && std::strcmp(n, name) == 0) return m;
        }
    }
    return nullptr;
}
struct V { float x, y, z; };
V vec(MonoObject* boxed) { V v{}; if (boxed) safe((std::uintptr_t)boxed + 0x10, &v, sizeof(v)); return v; }
std::wstring vtxt(const V& v) { std::wostringstream o; o << std::fixed << std::setprecision(3) << L"(" << v.x << L"," << v.y << L"," << v.z << L")"; return o.str(); }
std::wstring path() {
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe); auto s = p.find_last_of(L"\\/"); if (s != std::wstring::npos) p.resize(s);
    p += L"\\cheat_probe.txt"; return p;
}

struct Hit { std::uintptr_t object, animator, combatant; };
int scan_region(std::uintptr_t start, std::uintptr_t end, std::uintptr_t target_vt,
                std::uintptr_t anim_vt, std::uintptr_t cb_vt, Hit* hits, int max) {
    int n = 0;
    const std::uintptr_t* q = reinterpret_cast<const std::uintptr_t*>(start);
    const std::size_t words = (end - start) / 8;
    __try {
        for (std::size_t i = 0; i + 10 < words; ++i) {
            if (q[i] != target_vt) continue;
            if (n < max) hits[n++] = {start + i * 8, q[i + 4], q[i + 10]};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

DWORD WINAPI run(LPVOID) {
    Sleep(500);
    std::wofstream out(path(), std::ios::trunc);
    if (!out) return 1;
    if (!load()) { out << L"ERROR load\n"; out.flush(); return 2; }
    domain = api.root();
    if (!domain) { out << L"ERROR domain\n"; out.flush(); return 3; }
    MonoThread* t = att(domain);
    feach();
    out << L"images=" << images.size() << L"\n"; out.flush();

    MonoClass* jetpack = find_class_any("Jetpack");
    MonoClass* conta = find_class_any("ContaJetpack");
    MonoClass* auto_sensacao = find_class_any("AutoSensacao");
    MonoClass* boneco = find_class_any("Boneco");
    MonoClass* heroi_anim = find_class_any("HeroAnimator");
    MonoClass* animator = find_class("Animator");
    MonoClass* transform = find_class("Transform");
    MonoClass* combatant = find_class_any("Combatant");
    MonoClass* jogador = find_class_any("JogadorBR");
    out << L"classes jetpack=" << hx((std::uintptr_t)jetpack,16) << L" conta=" << hx((std::uintptr_t)conta,16)
        << L" auto=" << hx((std::uintptr_t)auto_sensacao,16) << L" boneco=" << hx((std::uintptr_t)boneco,16)
        << L" heroiAnim=" << hx((std::uintptr_t)heroi_anim,16) << L" animator=" << hx((std::uintptr_t)animator,16)
        << L" transform=" << hx((std::uintptr_t)transform,16) << L" combatant=" << hx((std::uintptr_t)combatant,16)
        << L" jogador=" << hx((std::uintptr_t)jogador,16) << L"\n"; out.flush();

    if (jetpack && conta) {
        MonoClassField* local_field = find_field(jetpack, "<Local>k__BackingField");
        std::uintptr_t local = 0; stv(vt(jetpack), local_field, &local);
        MonoClassField* conta_field = find_field(jetpack, "<Conta>k__BackingField");
        std::uintptr_t conta_obj = 0; if (local) fv((MonoObject*)local, conta_field, &conta_obj);
        MonoClassField* fuel_field = find_field(conta, "Combustivel");
        MonoClassField* factor_field = find_field(conta, "FatorConsumo");
        float fuel = 0, factor = 0;
        if (conta_obj && fuel_field) fv((MonoObject*)conta_obj, fuel_field, &fuel);
        if (conta_obj && factor_field) fv((MonoObject*)conta_obj, factor_field, &factor);
        out << L"JETPACK local=0x" << hx(local,16) << L" conta=0x" << hx(conta_obj,16)
            << L" fuel_type=" << (fuel_field ? tname(fuel_field) : "?")
            << L" fuel_off=" << (fuel_field ? std::to_wstring(api.field_offset(fuel_field)) : L"?")
            << L" fuel=" << fuel << L" factor=" << factor << L"\n"; out.flush();
    }

    if (jogador && combatant) {
        std::uintptr_t local = 0;
        stv(vt(jogador), find_field(jogador, "<Local>k__BackingField"), &local);
        std::uintptr_t cb = 0;
        fv((MonoObject*)local, find_field(jogador, "combatant"), &cb);
        MonoClassField* hp = find_field(combatant, "<Hp>k__BackingField");
        MonoClassField* max = find_field(combatant, "maxHp");
        float hpv = 0, maxv = 0;
        if (cb && hp) fv((MonoObject*)cb, hp, &hpv);
        if (cb && max) fv((MonoObject*)cb, max, &maxv);
        out << L"LOCAL jogador=0x" << hx(local,16) << L" combatant=0x" << hx(cb,16)
            << L" hp_type=" << (hp ? tname(hp) : "?") << L" hp_off=" << (hp ? std::to_wstring(api.field_offset(hp)) : L"?")
            << L" hp=" << hpv << L"/" << maxv << L"\n"; out.flush();
    }

    if (auto_sensacao && boneco && heroi_anim && animator && transform) {
        std::uintptr_t list = 0;
        stv(vt(auto_sensacao), find_field(auto_sensacao, "BonecoHerois"), &list);
        std::uintptr_t items = 0; int count = 0;
        if (list) { safe(list + 0x10, &items, 8); safe(list + 0x18, &count, 4); }
        out << L"BONECOS list=0x" << hx(list,16) << L" items=0x" << hx(items,16) << L" count=" << count << L"\n"; out.flush();
        MonoClass* list_class = goc((MonoObject*)list);
        if (list_class) out << L"list_class=" << (cname(list_class) ? cname(list_class) : "?") << L"\n"; out.flush();
        MonoMethod* get_bone = find_method(animator, "GetBoneTransform");
        MonoProperty* pos_prop = find_prop(transform, "position");
        const int total = count > 0 && count < 6 ? count : 0;
        for (int i = 0; i < total; ++i) {
            std::uintptr_t b = 0;
            void* s = slot((MonoArray*)items, i);
            if (!s || !safe((std::uintptr_t)s, &b, 8) || !b) continue;
            std::uintptr_t dono = 0, anim = 0, go = 0;
            fv((MonoObject*)b, find_field(boneco, "Dono"), &dono);
            fv((MonoObject*)b, find_field(boneco, "Anim"), &anim);
            fv((MonoObject*)b, find_field(boneco, "Go"), &go);
            MonoClass* anim_class = goc((MonoObject*)anim);
            out << L"B " << i << L" obj=0x" << hx(b,16) << L" dono=0x" << hx(dono,16)
                << L" anim=0x" << hx(anim,16) << L" anim_class=" << (anim_class && cname(anim_class) ? cname(anim_class) : "?")
                << L" go=0x" << hx(go,16) << L"\n"; out.flush();
            std::uintptr_t unity_animator = 0;
            if (anim_class && heroi_anim && (std::strcmp(cname(anim_class), "HeroAnimator") == 0))
                fv((MonoObject*)anim, find_field(heroi_anim, "animator"), &unity_animator);
            else if (anim_class && animator && (std::strcmp(cname(anim_class), "Animator") == 0))
                unity_animator = anim;
            out << L"  unity_animator=0x" << hx(unity_animator,16) << L"\n"; out.flush();
            if (unity_animator && get_bone && pos_prop) {
                int id = 11; void* args[] = {&id};
                MonoObject* tr = inv(get_bone, (MonoObject*)unity_animator, args);
                out << L"  head_tr=0x" << hx((std::uintptr_t)tr,16) << L" pos=" << vtxt(vec(prop(pos_prop, tr))) << L"\n"; out.flush();
            }
        }
    }

    out << L"DONE\n"; out.flush();
    det(t);
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(m);
        HANDLE h = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
