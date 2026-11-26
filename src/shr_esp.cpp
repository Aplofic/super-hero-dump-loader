// shr_esp.dll — ESP simples, somente leitura.
// Não escreve na memória do jogo nem suspende threads.
// A janela é um overlay layered/topmost; o jogo continua respondendo normalmente.
//
// Teclas:
//   F1  ligar/desligar ESP
//   F2  caixa
//   F3  esqueleto
//   F4  barra de HP
//   F5  nome
//   F6  menu
//   F7  HP infinito
//   F8  combustível infinito
//   F9  religar tudo
//
// Nota: o ESP é somente leitura. As duas escritas pontuais (F7/F8) usam
// offsets confirmados e não instalam hooks nem suspendem threads do jogo.
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace {

// ---------- Mono ----------
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
using StringToUtf8Fn = char*(*)(MonoString*);
using FreeFn = void(*)(void*);

struct Api {
    HMODULE module{};
    RootDomainFn root{}; AssemblyForeachFn foreach{}; AssemblyImageFn image{};
    ClassFromNameFn from_name{}; ClassNameFn class_name{}; ClassParentFn parent{};
    ClassGetFieldsFn fields{}; FieldNameFn field_name{}; FieldOffsetFn field_offset{};
    FieldValueFn field_value{}; ClassGetPropertiesFn props{}; PropertyGetNameFn prop_name{};
    PropertyGetValueFn prop_value{}; ClassGetMethodsFn methods{}; MethodGetNameFn method_name{};
    RuntimeInvokeFn invoke{}; ObjectGetClassFn object_class{}; ClassVtableFn vtable{};
    StaticValueFn static_value{}; AttachFn attach{}; DetachFn detach{};
    ArrayLengthFn array_length{}; ArrayAddrFn array_addr{};
    StringToUtf8Fn utf8{}; FreeFn free{};
} api;

MonoDomain* g_domain = nullptr;
std::vector<MonoImage*> g_images;

template<typename T> T sym(const char* n) { return reinterpret_cast<T>(GetProcAddress(api.module, n)); }

bool load_api() {
    api.module = GetModuleHandleW(L"mono-2.0-bdwgc.dll");
    if (!api.module) api.module = LoadLibraryW(L"mono-2.0-bdwgc.dll");
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
    api.array_length = sym<ArrayLengthFn>("mono_array_length");
    api.array_addr = sym<ArrayAddrFn>("mono_array_addr_with_size");
    api.utf8 = sym<StringToUtf8Fn>("mono_string_to_utf8");
    api.free = sym<FreeFn>("mono_free");
    return api.root && api.foreach && api.from_name && api.fields && api.props &&
           api.field_value && api.invoke && api.array_length && api.array_addr;
}

void image_callback(void* data, void*);

bool safe_copy(std::uintptr_t address, void* dst, std::size_t size) {
    __try { std::memcpy(dst, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
MonoThread* guarded_attach(MonoDomain* d) { __try { return api.attach(d); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void guarded_detach(MonoThread* t) { __try { if (t) api.detach(t); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
void guarded_foreach() { __try { api.foreach(image_callback, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
MonoObject* guarded_prop(MonoProperty* p, MonoObject* o) { __try { return api.prop_value(p, o, nullptr, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
MonoObject* guarded_invoke(MonoMethod* m, MonoObject* o, void** a) { __try { return api.invoke(m, o, a, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
MonoVTable* guarded_vtable(MonoClass* c) { __try { return api.vtable(g_domain, c); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void guarded_static(MonoVTable* v, MonoClassField* f, void* d) { __try { api.static_value(v, f, d); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
void guarded_field(MonoObject* o, MonoClassField* f, void* d) { __try { api.field_value(o, f, d); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
// Escrita pontual: usa o offset real do campo, sem hooks e sem tocar em código.
void guarded_set_field(MonoObject* o, MonoClassField* f, const void* value, std::size_t size) {
    if (!o || !f) return;
    __try {
        const int offset = api.field_offset(f);
        if (offset < 0) return;
        std::memcpy(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(o) + offset), value, size);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
MonoClass* guarded_class(MonoObject* o) { __try { return api.object_class(o); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
std::uintptr_t guarded_len(MonoArray* a) { __try { return api.array_length(a); } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; } }
void* guarded_slot(MonoArray* a, std::uintptr_t i) { __try { return api.array_addr(a, 8, i); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
char* guarded_utf8(MonoString* s) { __try { return api.utf8(s); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
void guarded_free(void* p) { __try { if (p) api.free(p); } __except(EXCEPTION_EXECUTE_HANDLER) {} }

void image_callback(void* data, void*) {
    if (!data) return;
    MonoImage* image = api.image(static_cast<MonoAssembly*>(data));
    if (image) g_images.push_back(image);
}

MonoClass* find_class(const char* name) {
    for (MonoImage* image : g_images) {
        for (const char* ns : {"", "UnityEngine", "SuperHeroJetpack"}) {
            MonoClass* klass = api.from_name(image, ns, name);
            if (!klass) continue;
            const char* actual = api.class_name(klass);
            if (actual && std::strcmp(actual, name) == 0) return klass;
        }
    }
    return nullptr;
}
MonoClassField* find_field(MonoClass* klass, const char* name) {
    for (MonoClass* c = klass; c; c = api.parent(c)) {
        void* iterator = nullptr;
        while (true) {
            MonoClassField* field = api.fields(c, &iterator);
            if (!field) break;
            const char* actual = api.field_name(field);
            if (actual && std::strcmp(actual, name) == 0) return field;
        }
    }
    return nullptr;
}
MonoProperty* find_prop(MonoClass* klass, const char* name) {
    for (MonoClass* c = klass; c; c = api.parent(c)) {
        void* iterator = nullptr;
        while (true) {
            MonoProperty* prop = api.props(c, &iterator);
            if (!prop) break;
            const char* actual = api.prop_name(prop);
            if (actual && std::strcmp(actual, name) == 0) return prop;
        }
    }
    return nullptr;
}
MonoMethod* find_method(MonoClass* klass, const char* name) {
    for (MonoClass* c = klass; c; c = api.parent(c)) {
        void* iterator = nullptr;
        while (true) {
            MonoMethod* method = api.methods(c, &iterator);
            if (!method) break;
            const char* actual = api.method_name(method);
            if (actual && std::strcmp(actual, name) == 0) return method;
        }
    }
    return nullptr;
}

// ---------- Matemática ----------
struct Vec3 { float x{}, y{}, z{}; };
struct Vec4 { float x{}, y{}, z{}, w{}; };
struct Mat4 { float m[16]{}; };

Vec4 mul(const Mat4& a, const Vec4& v) {
    return {
        a.m[0]*v.x + a.m[4]*v.y + a.m[8]*v.z + a.m[12]*v.w,
        a.m[1]*v.x + a.m[5]*v.y + a.m[9]*v.z + a.m[13]*v.w,
        a.m[2]*v.x + a.m[6]*v.y + a.m[10]*v.z + a.m[14]*v.w,
        a.m[3]*v.x + a.m[7]*v.y + a.m[11]*v.z + a.m[15]*v.w
    };
}
Vec3 read_vec3(MonoObject* boxed) {
    Vec3 v{}; if (boxed) safe_copy(reinterpret_cast<std::uintptr_t>(boxed) + 0x10, &v, sizeof(v)); return v;
}
Mat4 read_mat4(MonoObject* boxed) {
    Mat4 m{}; if (boxed) safe_copy(reinterpret_cast<std::uintptr_t>(boxed) + 0x10, m.m, sizeof(m.m)); return m;
}
bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// ---------- Estado compartilhado ----------
struct Target {
    std::string name;
    Vec3 head{}, aim{};
    float hp{}, max_hp{};
    bool is_player{};
    bool valid{};
    std::uintptr_t object{}; // Combatante Mono, usado para casar com BotAnimator.
};

struct Bone { Vec3 pos{}; bool valid{}; };
struct EnemySkeleton {
    std::uintptr_t combatant{};
    Bone bones[20];
    bool valid{};
};

Mat4 g_view{}, g_proj{};
Vec3 g_right{1, 0, 0}, g_up{0, 1, 0};
bool g_camera_valid{};
std::vector<Target> g_targets;
Bone g_bones[20];
std::vector<EnemySkeleton> g_enemy_bones;
bool g_esp_on{true}, g_box{true}, g_skeleton{true}, g_hp{true}, g_names{true}, g_menu{true};
bool g_inf_hp{true}, g_inf_fuel{true};

// Campos/métodos resolvidos uma vez na inicialização.
MonoClassField* f_all{};
MonoClassField* f_display_name{}, *f_is_player{}, *f_hp{}, *f_max_hp{};
MonoClassField* f_jogador_local{}, *f_jogador_combatant{}, *f_jogador_hero{};
MonoClassField* f_hero_animator{};
MonoClassField* f_jetpack_local{}, *f_jetpack_conta{}, *f_fuel{};
MonoClassField* f_bot_animator{}, *f_bot_combatant{};
MonoProperty* p_aim{}, *p_head{}, *p_camera_main{}, *p_view{}, *p_proj{}, *p_position{};
MonoMethod* m_get_bone{};
MonoClass* g_animator_class{}, *g_combatant_class{}, *g_transform_class{};
std::uintptr_t g_bot_vtable{}, g_animator_vtable{}, g_combatant_vtable{};
DWORD g_last_bot_scan{}, g_last_status{};
int g_bot_candidates{}, g_valid_enemy_bones{}, g_bot_vt_ok{}, g_bot_target_ok{};
std::uintptr_t g_scanned_bytes{};

// ---------- Snapshot ----------
void refresh_camera() {
    MonoObject* camera = guarded_prop(p_camera_main, nullptr);
    if (!camera) { g_camera_valid = false; return; }
    g_view = read_mat4(guarded_prop(p_view, camera));
    g_proj = read_mat4(guarded_prop(p_proj, camera));
    // Unity guarda as matrizes por colunas. A linha 0/1 da view são os eixos right/up no mundo.
    g_right = {g_view.m[0], g_view.m[4], g_view.m[8]};
    g_up = {g_view.m[1], g_view.m[5], g_view.m[9]};
    const float rl = std::sqrt(g_right.x*g_right.x + g_right.y*g_right.y + g_right.z*g_right.z);
    const float ul = std::sqrt(g_up.x*g_up.x + g_up.y*g_up.y + g_up.z*g_up.z);
    if (rl > 0.001f) { g_right.x/=rl; g_right.y/=rl; g_right.z/=rl; }
    if (ul > 0.001f) { g_up.x/=ul; g_up.y/=ul; g_up.z/=ul; }
    g_camera_valid = true;
}

std::string read_name(MonoObject* string_object) {
    if (!string_object) return {};
    char* utf = guarded_utf8(reinterpret_cast<MonoString*>(string_object));
    if (!utf) return {};
    char buffer[128]{};
    safe_copy(reinterpret_cast<std::uintptr_t>(utf), buffer, sizeof(buffer) - 1);
    guarded_free(utf);
    buffer[sizeof(buffer) - 1] = '\0';
    return std::string(buffer);
}

void refresh_targets() {
    g_targets.clear();
    std::uintptr_t list = 0;
    guarded_static(guarded_vtable(g_combatant_class), f_all, &list);
    if (!list) return;
    MonoClass* list_class = guarded_class(reinterpret_cast<MonoObject*>(list));
    MonoClassField* items_field = find_field(list_class, "_items");
    MonoClassField* size_field = find_field(list_class, "_size");
    if (!items_field || !size_field) return;

    std::uintptr_t items = 0;
    int count = 0;
    guarded_field(reinterpret_cast<MonoObject*>(list), items_field, &items);
    guarded_field(reinterpret_cast<MonoObject*>(list), size_field, &count);
    if (!items || count <= 0) return;
    count = std::min(count, 128);

    const std::uintptr_t length = guarded_len(reinterpret_cast<MonoArray*>(items));
    const std::uintptr_t total = std::min(static_cast<std::uintptr_t>(count), length);
    for (std::uintptr_t i = 0; i < total; ++i) {
        void* slot = guarded_slot(reinterpret_cast<MonoArray*>(items), i);
        std::uintptr_t object = 0;
        if (!slot || !safe_copy(reinterpret_cast<std::uintptr_t>(slot), &object, sizeof(object)) || !object)
            continue;
        Target target;
        target.object = object;
        target.head = read_vec3(guarded_prop(p_head, reinterpret_cast<MonoObject*>(object)));
        target.aim = read_vec3(guarded_prop(p_aim, reinterpret_cast<MonoObject*>(object)));
        if (!finite3(target.head) || !finite3(target.aim)) continue;
        std::uintptr_t name_ptr = 0;
        if (f_display_name) guarded_field(reinterpret_cast<MonoObject*>(object), f_display_name, &name_ptr);
        target.name = read_name(reinterpret_cast<MonoObject*>(name_ptr));
        if (f_is_player) guarded_field(reinterpret_cast<MonoObject*>(object), f_is_player, &target.is_player);
        if (f_hp) guarded_field(reinterpret_cast<MonoObject*>(object), f_hp, &target.hp);
        if (f_max_hp) guarded_field(reinterpret_cast<MonoObject*>(object), f_max_hp, &target.max_hp);
        target.valid = target.hp > 0.0f && target.max_hp > 0.0f;
        if (target.valid) g_targets.push_back(std::move(target));
    }
}

void read_bones(std::uintptr_t animator, Bone* out) {
    for (int i = 0; i < 20; ++i) out[i].valid = false;
    if (!animator || !m_get_bone || !p_position) return;
    // Valida o vtable antes de invocar: evita chamar GetBoneTransform em objeto inválido.
    std::uintptr_t vtable = 0;
    if (!safe_copy(animator, &vtable, sizeof(vtable)) || vtable != g_animator_vtable) return;
    for (int i = 0; i < 20; ++i) {
        int id = i;
        void* args[] = {&id};
        MonoObject* transform = guarded_invoke(m_get_bone, reinterpret_cast<MonoObject*>(animator), args);
        if (!transform) continue;
        Vec3 position = read_vec3(guarded_prop(p_position, transform));
        if (!finite3(position)) continue;
        out[i] = {position, true};
    }
}

void refresh_bones(MonoClass* jogador_class) {
    for (Bone& bone : g_bones) bone.valid = false;
    if (!jogador_class) return;
    std::uintptr_t local = 0;
    guarded_static(guarded_vtable(jogador_class), f_jogador_local, &local);
    if (!local) return;
    std::uintptr_t hero = 0;
    guarded_field(reinterpret_cast<MonoObject*>(local), f_jogador_hero, &hero);
    if (!hero) return;
    MonoClass* hero_class = guarded_class(reinterpret_cast<MonoObject*>(hero));
    std::uintptr_t animator = 0;
    if (hero_class) guarded_field(reinterpret_cast<MonoObject*>(hero), find_field(hero_class, "animator"), &animator);
    if (!animator) return;
    read_bones(animator, g_bones);
}

// Escrita pontual: vida = maxHp e combustível = 100 a cada tick.
void refresh_godmode(MonoClass* jogador_class, MonoClass* jetpack_class, MonoClass* conta_class) {
    if (g_inf_hp && jogador_class) {
        std::uintptr_t local = 0;
        guarded_static(guarded_vtable(jogador_class), f_jogador_local, &local);
        std::uintptr_t combatant = 0;
        if (local) guarded_field(reinterpret_cast<MonoObject*>(local), f_jogador_combatant, &combatant);
        if (combatant) {
            float max_hp = 0;
            guarded_field(reinterpret_cast<MonoObject*>(combatant), f_max_hp, &max_hp);
            if (max_hp > 0.0f && std::isfinite(max_hp)) {
                float value = max_hp;
                guarded_set_field(reinterpret_cast<MonoObject*>(combatant), f_hp, &value, sizeof(value));
            }
        }
    }
    if (g_inf_fuel && jetpack_class && conta_class) {
        std::uintptr_t local = 0;
        guarded_static(guarded_vtable(jetpack_class), f_jetpack_local, &local);
        std::uintptr_t conta = 0;
        if (local) guarded_field(reinterpret_cast<MonoObject*>(local), f_jetpack_conta, &conta);
        if (conta) {
            const float full = 100.0f;
            guarded_set_field(reinterpret_cast<MonoObject*>(conta), f_fuel, &full, sizeof(full));
        }
    }
}

struct BotHit { std::uintptr_t object, animator, combatant; };
int scan_region(std::uintptr_t start, std::uintptr_t end, BotHit* hits, int max_hits) {
    int count = 0;
    const std::uintptr_t* words = reinterpret_cast<const std::uintptr_t*>(start);
    const std::size_t total = (end - start) / sizeof(std::uintptr_t);
    __try {
        for (std::size_t i = 0; i + 10 < total; ++i) {
            if (words[i] != g_bot_vtable) continue;
            if (count < max_hits) hits[count++] = {start + i * sizeof(std::uintptr_t), words[i + 4], words[i + 10]};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return count;
}

bool is_live_target(std::uintptr_t combatant) {
    if (!combatant) return false;
    for (const Target& target : g_targets)
        if (target.object == combatant) return true;
    return false;
}

// Varredura leve do heap Mono à procura de BotAnimator; só invoca em animadores
// que pertencem a combatantes vivos da lista Combatant::All.
void refresh_enemy_bones() {
    g_enemy_bones.clear();
    g_bot_candidates = 0;
    g_valid_enemy_bones = 0;
    g_bot_vt_ok = 0;
    g_bot_target_ok = 0;
    g_scanned_bytes = 0;
    if (!g_bot_vtable || !g_animator_vtable || !g_combatant_vtable) return;

    // O heap Mono muda de endereço a cada partida: varrer todas as regiões
    // privadas com leitura/gravação, em vez de usar um intervalo fixo.
    MEMORY_BASIC_INFORMATION mbi{};
    for (std::uintptr_t address = 0x10000ULL; address < 0x7FFFFFFFFFFFULL;) {
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) { address += 0x1000; continue; }
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t end = base + mbi.RegionSize;
        if (end <= base) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE &&
            mbi.RegionSize <= (1u << 29)) {
            g_scanned_bytes += mbi.RegionSize;
            BotHit hits[96];
            const int count = scan_region(base, end, hits, 96);
            for (int i = 0; i < count; ++i) {
                ++g_bot_candidates;
                const std::uintptr_t animator = hits[i].animator;
                const std::uintptr_t combatant = hits[i].combatant;
                std::uintptr_t anim_vt = 0, combatant_vt = 0;
                if (animator && (!safe_copy(animator, &anim_vt, sizeof(anim_vt)) || anim_vt != g_animator_vtable)) continue;
                if (combatant && (!safe_copy(combatant, &combatant_vt, sizeof(combatant_vt)) || combatant_vt != g_combatant_vtable)) continue;
                ++g_bot_vt_ok;
                if (!is_live_target(combatant)) continue;
                ++g_bot_target_ok;
                EnemySkeleton skeleton{};
                skeleton.combatant = combatant;
                read_bones(animator, skeleton.bones);
                bool any = false;
                for (const Bone& bone : skeleton.bones) any = any || bone.valid;
                skeleton.valid = any;
                if (skeleton.valid) {
                    for (const Bone& bone : skeleton.bones) if (bone.valid) ++g_valid_enemy_bones;
                    g_enemy_bones.push_back(std::move(skeleton));
                }
                if (g_enemy_bones.size() >= 32) return;
            }
        }
        address = end;
    }
}

void write_status() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash);
    path += L"\\esp_status.txt";
    std::wofstream out(path, std::ios::trunc);
    if (!out) return;
    out << L"targets=" << g_targets.size()
        << L" bot_candidates=" << g_bot_candidates
        << L" vt_ok=" << g_bot_vt_ok << L" target_ok=" << g_bot_target_ok
        << L" scanned_mb=" << (g_scanned_bytes >> 20)
        << L" enemy_skeletons=" << g_enemy_bones.size()
        << L" enemy_bones=" << g_valid_enemy_bones
        << L" local_bones=" << (g_bones[0].valid ? 20 : 0)
        << L" esp=" << g_esp_on << L" box=" << g_box << L" skeleton=" << g_skeleton
        << L" hp_bar=" << g_hp << L" names=" << g_names << L" menu=" << g_menu
        << L" hp_inf=" << g_inf_hp << L" fuel_inf=" << g_inf_fuel
        << L"\n";
}

// ---------- Projeção e desenho ----------
struct Point { float x{}, y{}; bool visible{}; };

Point project(const Vec3& world, int width, int height) {
    if (!g_camera_valid) return {};
    Vec4 view = mul(g_view, {world.x, world.y, world.z, 1.0f});
    Vec4 clip = mul(g_proj, {view.x, view.y, view.z, 1.0f});
    if (std::abs(clip.w) < 0.0001f) return {};
    const float nx = clip.x / clip.w;
    const float ny = clip.y / clip.w;
    if (nx < -1.2f || nx > 1.2f || ny < -1.2f || ny > 1.2f) return {};
    return {(nx * 0.5f + 0.5f) * static_cast<float>(width),
            (0.5f - ny * 0.5f) * static_cast<float>(height), true};
}

Vec3 add_scaled(const Vec3& base, const Vec3& axis, float scale) {
    return {base.x + axis.x * scale, base.y + axis.y * scale, base.z + axis.z * scale};
}

void line(HDC dc, const Point& a, const Point& b) {
    if (!a.visible || !b.visible) return;
    MoveToEx(dc, static_cast<int>(a.x), static_cast<int>(a.y), nullptr);
    LineTo(dc, static_cast<int>(b.x), static_cast<int>(b.y));
}
void rect_lines(HDC dc, const Point& a, const Point& b) {
    if (!a.visible || !b.visible) return;
    Rectangle(dc, static_cast<int>(std::min(a.x, b.x)), static_cast<int>(std::min(a.y, b.y)),
                  static_cast<int>(std::max(a.x, b.x)), static_cast<int>(std::max(a.y, b.y)));
}

void draw_target(HDC dc, const Target& target, int width, int height) {
    const Vec3 top_world = add_scaled(target.head, g_up, 0.22f);
    const Vec3 bottom_world = add_scaled(target.aim, g_up, -1.45f);
    const float half = 0.38f;
    const Point tl = project(add_scaled(top_world, g_right, -half), width, height);
    const Point tr = project(add_scaled(top_world, g_right, half), width, height);
    const Point bl = project(add_scaled(bottom_world, g_right, -half), width, height);
    const Point br = project(add_scaled(bottom_world, g_right, half), width, height);
    if (!tl.visible || !tr.visible || !bl.visible || !br.visible) return;

    const COLORREF color = target.is_player ? RGB(80, 255, 120) : RGB(255, 80, 80);
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    // Rectangle() também preenche o interior; sem NULL_BRUSH ele fica branco.
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    const int left = static_cast<int>(std::min({tl.x, tr.x, bl.x, br.x}));
    const int right = static_cast<int>(std::max({tl.x, tr.x, bl.x, br.x}));
    const int top = static_cast<int>(std::min({tl.y, tr.y, bl.y, br.y}));
    const int bottom = static_cast<int>(std::max({tl.y, tr.y, bl.y, br.y}));
    if (g_box) Rectangle(dc, left, top, right, bottom);

    if (g_names && !target.name.empty()) {
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        TextOutA(dc, left, top - 15, target.name.c_str(), static_cast<int>(target.name.size()));
    }
    if (g_hp && target.max_hp > 0.0f) {
        const int bar_w = std::max(24, right - left);
        const int bar_top = top - 8;
        HBRUSH bg = CreateSolidBrush(RGB(60, 20, 20));
        HBRUSH fg = CreateSolidBrush(target.is_player ? RGB(80, 255, 120) : RGB(255, 120, 60));
        RECT r{left, bar_top, left + bar_w, bar_top + 5};
        FillRect(dc, &r, bg);
        const float fraction = std::clamp(target.hp / target.max_hp, 0.0f, 1.0f);
        r.right = left + static_cast<int>(bar_w * fraction);
        FillRect(dc, &r, fg);
        DeleteObject(bg);
        DeleteObject(fg);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_skeleton_at(HDC dc, const Bone* bones, COLORREF color, int width, int height) {
    bool any = false;
    for (int i = 0; i < 20; ++i) any = any || bones[i].valid;
    if (!any) return;
    Point p[20];
    for (int i = 0; i < 20; ++i)
        p[i] = bones[i].valid ? project(bones[i].pos, width, height) : Point{};

    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    static const int links[][2] = {
        {0,7},{7,8},{8,9},{9,10},{10,11},
        {9,12},{12,14},{14,16},{16,18},
        {9,13},{13,15},{15,17},{17,19},
        {0,1},{1,3},{3,5},
        {0,2},{2,4},{4,6}
    };
    for (const auto& link : links) line(dc, p[link[0]], p[link[1]]);
    for (int i = 0; i < 20; ++i) {
        if (!p[i].visible) continue;
        Rectangle(dc, static_cast<int>(p[i].x) - 2, static_cast<int>(p[i].y) - 2,
                      static_cast<int>(p[i].x) + 2, static_cast<int>(p[i].y) + 2);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_skeleton(HDC dc, int width, int height) {
    for (const EnemySkeleton& skeleton : g_enemy_bones)
        if (skeleton.valid) draw_skeleton_at(dc, skeleton.bones, RGB(255, 60, 60), width, height);
    draw_skeleton_at(dc, g_bones, RGB(0, 240, 255), width, height);
}

void draw_menu(HDC dc) {
    if (!g_menu) return;
    const int x = 12, y = 12, w = 224, h = 178;
    HBRUSH backdrop = CreateSolidBrush(RGB(18, 18, 18));
    HPEN outline = CreatePen(PS_SOLID, 1, RGB(0, 200, 255));
    RECT panel{x, y, x + w, y + h};
    FillRect(dc, &panel, backdrop);
    HPEN old = static_cast<HPEN>(SelectObject(dc, outline));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, old);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 220, 255));
    TextOutA(dc, x + 10, y + 8, "SHR ESP", 7);
    SetTextColor(dc, RGB(230, 230, 230));
    const char* lines[] = {
        g_esp_on ? "F1  ESP  [ON]" : "F1  ESP  [OFF]",
        g_box ? "F2  Box  [ON]" : "F2  Box  [OFF]",
        g_skeleton ? "F3  Skeleton [ON]" : "F3  Skeleton [OFF]",
        g_hp ? "F4  HP bar [ON]" : "F4  HP bar [OFF]",
        g_names ? "F5  Nome [ON]" : "F5  Nome [OFF]",
        g_menu ? "F6  Menu [ON]" : "F6  Menu [OFF]",
        g_inf_hp ? "F7  HP infinito [ON]" : "F7  HP infinito [OFF]",
        g_inf_fuel ? "F8  Combustivel [ON]" : "F8  Combustivel [OFF]",
        "F9  Religar tudo"
    };
    int row = y + 28;
    for (const char* text : lines) {
        TextOutA(dc, x + 10, row, text, static_cast<int>(std::strlen(text)));
        row += 16;
    }
    DeleteObject(outline);
    DeleteObject(backdrop);
}

void draw(HDC dc, int width, int height) {
    RECT area{0, 0, width, height};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &area, black);
    DeleteObject(black);
    if (!g_esp_on) { draw_menu(dc); return; }
    for (const Target& target : g_targets) draw_target(dc, target, width, height);
    if (g_skeleton) draw_skeleton(dc, width, height);
    draw_menu(dc);
}

// ---------- Janela e loop ----------
HWND g_hwnd{};
int g_width{}, g_height{};

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc) { draw(dc, g_width, g_height); EndPaint(hwnd, &ps); }
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

HMODULE this_module() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&window_proc), &module);
    return module ? module : reinterpret_cast<HMODULE>(GetModuleHandle(nullptr));
}

bool create_window() {
    g_width = GetSystemMetrics(SM_CXSCREEN);
    g_height = GetSystemMetrics(SM_CYSCREEN);
    // Classe única por DLL: injetar uma versão nova não pode reutilizar o WndProc antigo.
    wchar_t class_name[96];
    swprintf(class_name, 96, L"SHR_ESP_OVERLAY_%p", reinterpret_cast<void*>(this_module()));
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = this_module();
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                             class_name, L"SHR ESP", WS_POPUP,
                             0, 0, g_width, g_height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return false;
    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(g_hwnd, SW_SHOWNA);
    return true;
}

bool pressed(int virtual_key) { return (GetAsyncKeyState(virtual_key) & 0x8000) != 0; }

void poll_keys() {
    static bool previous[9]{};
    const int keys[9] = {VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9};
    bool current[9]{};
    for (int i = 0; i < 9; ++i) current[i] = pressed(keys[i]);
    for (int i = 0; i < 9; ++i) {
        if (current[i] && !previous[i]) {
            if (i == 0) g_esp_on = !g_esp_on;
            else if (i == 1) g_box = !g_box;
            else if (i == 2) g_skeleton = !g_skeleton;
            else if (i == 3) g_hp = !g_hp;
            else if (i == 4) g_names = !g_names;
            else if (i == 5) g_menu = !g_menu;
            else if (i == 6) g_inf_hp = !g_inf_hp;
            else if (i == 7) g_inf_fuel = !g_inf_fuel;
            else if (i == 8) {
                g_esp_on = g_box = g_skeleton = g_hp = g_names = g_menu = true;
                g_inf_hp = g_inf_fuel = true;
            }
        }
        previous[i] = current[i];
    }
}

DWORD WINAPI esp_thread(LPVOID) {
    if (!load_api()) return 1;
    g_domain = api.root();
    if (!g_domain) return 2;
    MonoThread* thread = guarded_attach(g_domain);
    if (!thread) return 3;
    guarded_foreach();

    MonoClass* camera_class = find_class("Camera");
    MonoClass* jogador_class = find_class("JogadorBR");
    MonoClass* jetpack_class = find_class("Jetpack");
    MonoClass* conta_class = find_class("ContaJetpack");
    MonoClass* bot_class = find_class("BotAnimator");
    g_animator_class = find_class("Animator");
    g_combatant_class = find_class("Combatant");
    g_transform_class = find_class("Transform");
    if (!camera_class || !g_combatant_class || !jogador_class || !g_animator_class || !g_transform_class) {
        guarded_detach(thread);
        return 4;
    }

    f_all = find_field(g_combatant_class, "All");
    f_display_name = find_field(g_combatant_class, "displayName");
    f_is_player = find_field(g_combatant_class, "isPlayer");
    f_hp = find_field(g_combatant_class, "<Hp>k__BackingField");
    f_max_hp = find_field(g_combatant_class, "maxHp");
    f_jogador_local = find_field(jogador_class, "<Local>k__BackingField");
    f_jogador_combatant = find_field(jogador_class, "combatant");
    f_jogador_hero = find_field(jogador_class, "hero");
    f_jetpack_local = jetpack_class ? find_field(jetpack_class, "<Local>k__BackingField") : nullptr;
    f_jetpack_conta = jetpack_class ? find_field(jetpack_class, "<Conta>k__BackingField") : nullptr;
    f_fuel = conta_class ? find_field(conta_class, "Combustivel") : nullptr;
    f_bot_animator = bot_class ? find_field(bot_class, "animator") : nullptr;
    f_bot_combatant = bot_class ? find_field(bot_class, "combatant") : nullptr;
    p_camera_main = find_prop(camera_class, "main");
    p_view = find_prop(camera_class, "worldToCameraMatrix");
    p_proj = find_prop(camera_class, "projectionMatrix");
    p_aim = find_prop(g_combatant_class, "AimPosition");
    p_head = find_prop(g_combatant_class, "HeadPosition");
    p_position = find_prop(g_transform_class, "position");
    m_get_bone = find_method(g_animator_class, "GetBoneTransform");
    g_bot_vtable = bot_class ? reinterpret_cast<std::uintptr_t>(guarded_vtable(bot_class)) : 0;
    g_animator_vtable = reinterpret_cast<std::uintptr_t>(guarded_vtable(g_animator_class));
    g_combatant_vtable = reinterpret_cast<std::uintptr_t>(guarded_vtable(g_combatant_class));

    if (!create_window()) { guarded_detach(thread); return 5; }

    MSG message{};
    for (;;) {
        poll_keys();
        refresh_godmode(jogador_class, jetpack_class, conta_class);
        if (g_esp_on) {
            refresh_camera();
            refresh_targets();
            refresh_bones(jogador_class);
            const DWORD now = GetTickCount();
            if (g_skeleton && now - g_last_bot_scan >= 500) {
                g_last_bot_scan = now;
                refresh_enemy_bones();
            }
        } else {
            g_enemy_bones.clear();
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        const DWORD status_now = GetTickCount();
        if (status_now - g_last_status >= 1000) {
            g_last_status = status_now;
            write_status();
        }
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                DestroyWindow(g_hwnd);
                guarded_detach(thread);
                return 0;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(33);
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, esp_thread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
