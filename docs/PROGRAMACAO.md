# Guia completo de programação, build e modificação

Este documento contém **tudo o que é necessário para compilar, executar e alterar** o projeto. É o complemento prático de [DUMP.md](DUMP.md) (onde e por quê) e [ESP.md](ESP.md) (como o overlay desenha).

## 1. Pré-requisitos

| Item | Detalhe |
|---|---|
| Sistema | Windows 10/11 **x64** |
| Compilador | Visual Studio com o workload **Desenvolvimento para desktop com C++** (testado em VS 18 Community; VS 2022 também serve) |
| SDK | Windows SDK (para `Windows.h`, `user32.lib`, `gdi32.lib`) |
| Jogo | Build `alfa-0.1-19`, com `mono-2.0-bdwgc.dll` na pasta do jogo |
| Permissão | Abrir o processo do jogo com o mesmo usuário; executar a CLI no mesmo login |
| Opcional | GitHub CLI (`gh`) para versionar |

Para achar o `vcvars64.bat` da sua instalação:

```powershell
Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat | Select-Object -ExpandProperty FullName
```

Exemplo usado nos testes:

```text
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
```

## 2. Mapa do código-fonte

| Arquivo | Gera | Função |
|---|---|---|
| `src/shr_common.h` | — | Helpers: `VirtualQueryEx`, leitura/escrita, enumeração de regiões |
| `src/shr_tool.cpp` | `bin/shr_tool.exe` | CLI externa (PID, módulos, varredura, ponteiros, AOB, injeção) |
| `src/shr_dump.cpp` | `shr_dump.dll` | Dump de metadata Mono → `offsets_dump.txt` |
| `src/shr_live.cpp` | `bin/shr_live.dll` | Resolve a cadeia do jogador → `live_pointers.txt` |
| `src/shr_esp.cpp` | `bin/shr_esp7.dll` | Overlay ESP com menu e godmode |
| `src/shr_cheat_probe.cpp` | sonda | Classes, offsets, vtables, caminhos de HP/combustível → `cheat_probe.txt` |
| `src/shr_bone_probe.cpp` | sonda | Índices e posições dos ossos → `bone_probe.txt` |
| `src/shr_fuel_probe.cpp` | sonda | Relê Jetpack/Conta/combustível → `cheat_probe.txt` |

Todas as sondas escrevem na pasta do jogo, ao lado de `Prototype.exe`, em `.shr-versao-rVkyRE\`.

## 3. Compilando tudo

Abra um **Prompt de Comando** (não PowerShell, para evitar problemas de aspas):

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\Users\Melinsky\Desktop\HerroDll\shr_tools\publish\src

cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- shr_tool.cpp /Fe:shr_tool.exe
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_dump.cpp /Fe:shr_dump.dll
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_live.cpp /Fe:shr_live.dll
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_esp.cpp /Fe:shr_esp7.dll /link user32.lib gdi32.lib
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_cheat_probe.cpp /Fe:shr_cheat_probe.dll
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_bone_probe.cpp /Fe:shr_bone_probe.dll
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_fuel_probe.cpp /Fe:shr_fuel_probe.dll
```

O que cada opção faz:

| Opção | Significado |
|---|---|
| `/std:c++17` | Linguagem C++17 |
| `/EHsc` | Exceções C++ + tratamento de exceções do Windows (`__try/__except`) usado nos `guarded_*` |
| `/O2` | Otimizar para velocidade |
| `/W4 /permissive-` | Avisos altos e modo estrito — ajuda a pegar ponteiros errados cedo |
| `/LD` | Gerar DLL (com `DllMain`) |
| `/Fe:nome` | Nome do binário de saída |
| `/link user32.lib gdi32.lib` | Necessário para janelas, teclas e desenho GDI do overlay |

**Regra importante:** se a DLL já está carregada no jogo, o linker não consegue sobrescrever o arquivo (`LNK1104`). Use um nome novo (`shr_esp8.dll`, etc.) ou reinicie o jogo.

## 4. Executando a CLI

```text
shr_tool.exe
```

A CLI é interativa; digite `help` para ver a lista. Comandos (o PID pode ser numérico ou nome do processo):

| Comando | Sintaxe | Exemplo |
|---|---|---|
| `pid` | `pid [nome]` | `pid Prototype.exe` |
| `modules` | `modules <PID>` | `modules 24968` |
| `regions` | `regions <PID>` | lista regiões legíveis |
| `value` | `value <PID> <hex> [4\|8]` | `value 624 00004843 4` (encontra `200.0f`) |
| `rescan` | `rescan <PID> <hex> [4\|8]` | filtra os hits anteriores |
| `ptrmap` | `ptrmap <PID> [limite]` | monta mapa de ponteiros (pesado) |
| `ptrpath` | `ptrpath <PID> <alvo> [profundidade] [offset_max]` | caminhos de ponteiro |
| `rootpath` | `rootpath <PID> <alvo> [profundidade] [offset_max]` | caminhos ancorados em módulos |
| `aob` | `aob <PID> <padrão>` | `aob 624 "48 8B 05 ?? ?? ?? ??"` (`??` = curinga) |
| `modfind` | `modfind <PID> <padrão>` | procura dentro dos módulos |
| `read` | `read <PID> <endereço> <tamanho>` | `read 624 0x1C7B5907860 8` |
| `write` | `write <PID> <endereço> <bytes>` | `write 624 0x... 00004843` |
| `inject` | `inject <PID> <caminho.dll>` | `inject 24968 C:\...\shr_esp7.dll` |
| `clear` / `help` / `quit` | sem PID | limpa hits, ajuda, sai |

Exemplo de uso não interativo (PowerShell):

```powershell
@"
inject 24968 C:\Users\Melinsky\Desktop\HerroDll\shr_tools\publish\bin\shr_esp7.dll
quit
"@ | & 'C:\...\shr_tools\publish\bin\shr_tool.exe'
```

> Evite `ptrmap`/`ptrpath` durante a partida: varrem milhões de endereços. Com a metadata do Mono resolvida, as cadeias diretas bastam.

## 5. Arquitetura interna de `shr_esp.cpp`

### Carregamento da API Mono

```cpp
template<typename T> T sym(const char* n) { return reinterpret_cast<T>(GetProcAddress(api.module, n)); }
bool load_api();   // resolve mono_get_root_domain, mono_thread_attach, mono_assembly_foreach,
                   // mono_class_from_name, mono_field_get_value, mono_property_get_value,
                   // mono_runtime_invoke, mono_array_get_length, etc.
```

O domínio vem de `mono_get_root_domain()` (ou da âncora `mono-2.0-bdwgc.dll+0x73FEF8`). A thread chamada `mono_thread_attach()` antes de qualquer reflection.

### Resolução de símbolos

```cpp
MonoClass*      find_class(const char* name);              // varre os namespaces conhecidos
MonoClassField* find_field(MonoClass*, const char*);
MonoProperty*   find_prop(MonoClass*, const char*);
MonoMethod*     find_method(MonoClass*, const char*);
```

Todos os ponteiros para o Mono passam por wrappers `__try/__except`:

```cpp
guarded_prop / guarded_invoke / guarded_vtable / guarded_field
guarded_static / guarded_set_field / guarded_utf8 / guarded_slot ...
```

Isso impede que um objeto inválido derrube a thread do overlay.

### Estado e refresh

| Função | O que faz |
|---|---|
| `refresh_camera()` | lê `Camera.main`, matrizes `worldToCameraMatrix` e `projectionMatrix` |
| `refresh_targets()` | enumera `Combatant::All`, lê nome/pontos/HP/isPlayer |
| `refresh_bones()` | ossos do jogador local via `HeroAnimator` |
| `refresh_godmode()` | aplica as duas escritas pontuais (F7/F8) |
| `refresh_enemy_bones()` | varre regiões RW por vtable de `BotAnimator` e valida contra `Combatant::All` |
| `write_status()` | grava `esp_status.txt` a cada 1 s |

### Desenho e janela

| Função | O que faz |
|---|---|
| `project()` | mundo → tela usando as matrizes da câmera |
| `draw_target()` | caixa, nome e barra de HP |
| `draw_skeleton_at()` / `draw_skeleton()` | 20 ossos, com `NULL_BRUSH` |
| `draw_menu()` / `draw()` | painel e composição final |
| `create_window()` / `window_proc()` | overlay `WS_EX_LAYERED\|TRANSPARENT\|TOPMOST`, classe por módulo |
| `poll_keys()` | F1–F9 com detecção de borda |
| `esp_thread()` / `DllMain()` | criação da thread e ciclo de vida |

## 6. Como estender o código

### 6.1 Adicionar um novo toggle

1. Declare o booleano ao lado dos outros em `shr_esp.cpp`:

```cpp
bool g_novo_recurso{true};
```

2. Inclua a tecla em `poll_keys()` — aumente os arrays de 9 para 10 e adicione o VK:

```cpp
const int keys[10] = {VK_F1, ..., VK_F9, VK_F10};
...
else if (i == 9) g_novo_recurso = !g_novo_recurso;
```

3. Respeite o flag em `draw()` ou no refresh correspondente.
4. Adicione a linha em `draw_menu()` e em `write_status()`.
5. Se for um recurso "religar tudo", inclua em `F9`.

### 6.2 Adicionar outra escrita pontual

Use sempre `guarded_set_field` com o offset descoberto no dump:

```cpp
MonoClassField* f = find_field(klass, "NomeDoCampo");
float value = 123.0f;
guarded_set_field(reinterpret_cast<MonoObject*>(obj), f, &value, sizeof(value));
```

Mantenha a regra do projeto: **apenas HP e combustível são escritos**. Qualquer outro campo deve continuar somente leitura.

### 6.3 Mudar cores ou estilo

As canetas são criadas com `CreatePen(PS_SOLID, 2, RGB(...))`. Antes de `Rectangle()` e das juntas, o código seleciona `GetStockObject(NULL_BRUSH)` e restaura o brush antigo — **não remova isso**, senão as caixas voltam a ficar brancas.

### 6.4 Adicionar outro tipo de inimigo

Repita o padrão de `BotAnimator`: descubra o vtable da classe no dump, varra regiões `MEM_PRIVATE`/`PAGE_READWRITE`, valide os campos e confirme que o `combatant` aparece em `Combatant::All` antes de invocar Mono.

### 6.5 Portar para outra versão do jogo

1. Abra a partida e descubra o PID atual (`pid Prototype.exe`).
2. Injete `shr_dump.dll`; confira `offsets_dump.txt` — a versão pode ter renomeado campos.
3. Injete `shr_live.dll` e `shr_fuel_probe.dll`; confira `live_pointers.txt` e `cheat_probe.txt`.
4. Atualize em `shr_esp.cpp`: nomes de classes/namespaces, `find_field`, `find_prop`, `find_method` e vtables.
5. Confira os offsets duros (`+0x20`, `+0xE0`, `+0x28`, etc.) contra o dump novo.
6. Compile com um nome novo de DLL e valide `esp_status.txt` dentro da partida.

## 7. Problemas comuns

| Sintoma | Causa | Solução |
|---|---|---|
| `LNK1104: não é possível abrir shr_esp.dll` | DLL carregada no jogo | Compile com nome novo ou reinicie o jogo |
| Teclas não mudam nada | `WndProc` antigo ainda desenhando | Use a classe por módulo (`SHR_ESP_OVERLAY_<módulo>`) e reinicie a sessão |
| `mono_domain_get()` retorna null | Thread remota sem attach | `mono_get_root_domain()` + `mono_thread_attach()` |
| `find_class_any("Transform")` acha classe errada | Namespace ausente | `find_class("Transform")` com namespace `UnityEngine` |
| `ContaJetpack` não é encontrada | Namespace diferente | Inclua `SuperHeroJetpack` na busca |
| Caixas brancas preenchidas | `Rectangle()` usa o brush atual | `GetStockObject(NULL_BRUSH)` antes de desenhar |
| `bot_candidates=0` em sessão nova | Faixa de heap fixa da sessão antiga | Use `shr_esp7.cpp` (varredura dinâmica) |
| `targets=0` | Ainda não entrou na partida | Entre na partida; o overlay começa automaticamente |
| Jogo congela após injeção | Chamada Mono em objeto inválido | Mantenha as validações de vtable e `Combatant::All` |

## 8. Checklist antes de alterar ou publicar

- [ ] Compilou sem erros com `/W4 /permissive-`.
- [ ] PID atual descoberto — nenhum endereço absoluto reutilizado de outra sessão.
- [ ] DLL injetada, janela `SHR_ESP_OVERLAY_*` visível e **uma** instância só.
- [ ] Jogo `Responding = True` depois de alguns minutos.
- [ ] `esp_status.txt` mostra `targets > 0`, `enemy_skeletons > 0` dentro da partida.
- [ ] Nenhum hook, nenhuma thread suspensa, nenhuma escrita além de HP e combustível.
- [ ] `README.md` e este guia atualizados se algum controle/off mudou.
