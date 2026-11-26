// shr_bone_probe.dll — somente leitura: ossos do jogador local.
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
struct MonoVTable; struct MonoThread; struct MonoObject;

using RootDomainFn = MonoDomain*(*)();
using AssemblyForeachFn = void(*)(void(*)(void*, void*), void*);
using AssemblyImageFn = MonoImage*(*)(MonoAssembly*);
using ClassFromNameFn = MonoClass*(*)(MonoImage*, const char*, const char*);
using ClassNameFn = const char*(*)(MonoClass*);
using ClassParentFn = MonoClass*(*)(MonoClass*);
using ClassGetFieldsFn = MonoClassField*(*)(MonoClass*, void**);
using FieldNameFn = const char*(*)(MonoClassField*);
using FieldOffsetFn = int(*)(MonoClassField*);
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

struct Api {
    HMODULE module{};
    RootDomainFn root{}; AssemblyForeachFn foreach{}; AssemblyImageFn image{};
    ClassFromNameFn from_name{}; ClassNameFn class_name{}; ClassParentFn parent{};
    ClassGetFieldsFn fields{}; FieldNameFn field_name{}; FieldOffsetFn field_offset{};
    FieldValueFn field_value{}; ClassGetPropertiesFn props{}; PropertyGetNameFn prop_name{};
    PropertyGetValueFn prop_value{}; ClassGetMethodsFn methods{}; MethodGetNameFn method_name{};
    RuntimeInvokeFn invoke{}; ObjectGetClassFn object_class{}; ClassVtableFn vtable{};
    StaticValueFn static_value{}; AttachFn attach{}; DetachFn detach{};
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
    return api.root && api.foreach && api.from_name && api.fields && api.props && api.invoke;
}

template<typename T> std::wstring hx(T v, int w) {
    std::wostringstream o; o << std::hex << std::uppercase << std::setw(w) << std::setfill(L'0') << (std::uint64_t)v; return o.str();
}
bool safe(std::uintptr_t a, void* d, std::size_t n) {
    __try { std::memcpy(d, (const void*)a, n); return true; } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
MonoThread* att(MonoDomain* d) { __try { return api.attach(d); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void det(MonoThread* t) { __try { if (t) api.detach(t); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
void feach(void(*cb)(void*, void*)) { __try { api.foreach(cb, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
MonoObject* prop(MonoProperty* p, MonoObject* o) { __try { return api.prop_value(p, o, nullptr, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
MonoObject* inv(MonoMethod* m, MonoObject* o, void** a) { __try { return api.invoke(m, o, a, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
MonoVTable* vt(MonoClass* c) { __try { return api.vtable(domain, c); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void stv(MonoVTable* v, MonoClassField* f, void* d) { __try { api.static_value(v, f, d); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
void fv(MonoObject* o, MonoClassField* f, void* d) { __try { api.field_value(o, f, d); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
MonoClass* goc(MonoObject* o) { __try { return api.object_class(o); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }

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
struct Mat { float m[16]{}; };
V vec(MonoObject* boxed) { V v{}; if (boxed) safe((std::uintptr_t)boxed + 0x10, &v, sizeof(v)); return v; }
std::wstring vtxt(const V& v) { std::wostringstream o; o << std::fixed << std::setprecision(3) << L"(" << v.x << L"," << v.y << L"," << v.z << L")"; return o.str(); }

std::wstring path() {
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe); auto s = p.find_last_of(L"\\/"); if (s != std::wstring::npos) p.resize(s);
    p += L"\\verify_probe.txt"; return p;
}

DWORD WINAPI run(LPVOID) {
    Sleep(500);
    std::wofstream out(path(), std::ios::trunc);
    if (!out) return 1;
    if (!load()) { out << L"ERROR load\n"; out.flush(); return 2; }
    domain = api.root();
    if (!domain) { out << L"ERROR domain\n"; out.flush(); return 3; }
    MonoThread* t = att(domain);
    feach(cb);
    out << L"domain=0x" << hx((std::uintptr_t)domain, 16) << L" images=" << images.size() << L"\n"; out.flush();

    MonoClass* jogador = find_class("JogadorBR");
    MonoClass* transform = find_class("Transform");
    MonoClass* animator_class = find_class("Animator");
    MonoClass* camera_class = find_class("Camera");
    MonoClass* combatant_class = find_class("Combatant");
    out << L"jogador=0x" << hx((std::uintptr_t)jogador, 16) << L" transform=0x" << hx((std::uintptr_t)transform, 16)
        << L" animator=0x" << hx((std::uintptr_t)animator_class, 16) << L" camera=0x" << hx((std::uintptr_t)camera_class, 16) << L"\n"; out.flush();
    if (!jogador || !transform || !animator_class) { det(t); return 4; }

    if (camera_class) {
        MonoObject* camera = prop(find_prop(camera_class, "main"), nullptr);
        Mat vm{}, pm{};
        if (camera) {
            safe((std::uintptr_t)prop(find_prop(camera_class, "worldToCameraMatrix"), camera) + 0x10, &vm, sizeof(vm));
            safe((std::uintptr_t)prop(find_prop(camera_class, "projectionMatrix"), camera) + 0x10, &pm, sizeof(pm));
        }
        out << L"camera_obj=0x" << hx((std::uintptr_t)camera, 16) << L"\nview=";
        for (float f : vm.m) out << L" " << f;
        out << L"\nproj=";
        for (float f : pm.m) out << L" " << f;
        out << L"\n"; out.flush();
    }

    if (combatant_class) {
        std::uintptr_t list = 0;
        MonoClassField* allf = find_field(combatant_class, "All");
        stv(vt(combatant_class), allf, &list);
        std::uintptr_t items = 0; int count = 0;
        if (list) { safe(list + 0x10, &items, 8); safe(list + 0x18, &count, 4); }
        out << L"list=0x" << hx(list, 16) << L" items=0x" << hx(items, 16) << L" count=" << count << L"\n"; out.flush();
        MonoProperty* pos_prop = find_prop(transform, "position");
        const int total = count > 0 ? (count < 5 ? count : 5) : 0;
        for (int i = 0; i < total; ++i) {
            std::uintptr_t obj = 0;
            safe(items + 0x20 + (std::size_t)i * 8, &obj, 8);
            std::uintptr_t aim = 0, head = 0; float hp = 0, maxhp = 0;
            safe(obj + 0x28, &aim, 8); safe(obj + 0x30, &head, 8);
            safe(obj + 0xE0, &hp, 4); safe(obj + 0xCC, &maxhp, 4);
            V a = vec(prop(pos_prop, (MonoObject*)aim));
            V h = vec(prop(pos_prop, (MonoObject*)head));
            out << L"T " << i << L" obj=0x" << hx(obj, 16) << L" hp=" << hp << L"/" << maxhp
                << L" aim=" << vtxt(a) << L" head=" << vtxt(h) << L"\n"; out.flush();
        }
    }

    std::uintptr_t local = 0;
    MonoClassField* lf = find_field(jogador, "<Local>k__BackingField");
    stv(vt(jogador), lf, &local);
    out << L"local=0x" << hx(local, 16) << L"\n"; out.flush();

    std::uintptr_t hero = 0;
    MonoClassField* hf = find_field(jogador, "hero");
    fv((MonoObject*)local, hf, &hero);
    out << L"hero=0x" << hx(hero, 16) << L"\n"; out.flush();

    MonoClass* hero_class = goc((MonoObject*)hero);
    const char* hero_class_name = hero_class ? api.class_name(hero_class) : nullptr;
    out << L"hero_class=" << (hero_class_name ? hero_class_name : "?") << L"\n"; out.flush();

    std::uintptr_t animator = 0;
    MonoClassField* af = find_field(hero_class, "animator");
    fv((MonoObject*)hero, af, &animator);
    out << L"animator=0x" << hx(animator, 16) << L"\n"; out.flush();

    MonoProperty* pos_prop = find_prop(transform, "position");
    MonoMethod* get_bone = find_method(animator_class, "GetBoneTransform");
    out << L"pos_prop=0x" << hx((std::uintptr_t)pos_prop, 16) << L" get_bone=0x" << hx((std::uintptr_t)get_bone, 16) << L"\n"; out.flush();

    static const char* names[] = {
        "Hips","LeftUpperLeg","RightUpperLeg","LeftLowerLeg","RightLowerLeg","LeftFoot","RightFoot",
        "Spine","Chest","UpperChest","Neck","Head","LeftShoulder","RightShoulder",
        "LeftUpperArm","RightUpperArm","LeftLowerArm","RightLowerArm","LeftHand","RightHand"
    };
    for (int i = 0; i < 20; ++i) {
        int id = i; void* args[] = {&id};
        MonoObject* tr = inv(get_bone, (MonoObject*)animator, args);
        out << L"BONE " << i << L" " << names[i] << L" tr=0x" << hx((std::uintptr_t)tr, 16)
            << L" pos=" << vtxt(vec(prop(pos_prop, tr))) << L"\n"; out.flush();
    }

    static const char* hero_fields[] = {"manequim","reference","animator","arPe","arAntA","arAntD","ahPe","ahDedo","modelo","gunHolder"};
    for (const char* name : hero_fields) {
        MonoClassField* f = find_field(hero_class, name);
        std::uintptr_t value = 0;
        if (f) fv((MonoObject*)hero, f, &value);
        out << L"HERO " << name << L"=0x" << hx(value, 16);
        if (value) out << L" pos=" << vtxt(vec(prop(pos_prop, (MonoObject*)value)));
        out << L"\n"; out.flush();
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
