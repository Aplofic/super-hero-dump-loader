# Super Hero Royale — offsets e ponteiros reais (25/11/2026)

## Alvo

- Jogo: `C:\Users\Melinsky\AppData\Local\SuperHeroRoyale\jogo\.shr-versao-rVkyRE\Prototype.exe`
- Engine: Unity **Mono** (`mono-2.0-bdwgc.dll`), não IL2CPP.
- PIDs usados:
  - `26176` — primeiro dump.
  - `27320` — dump refeito **dentro da partida** (iniciado 23:05:19).
  - `6992` — sessão final do resolvedor: `shr_live.dll` injetado dentro da partida (iniciado 23:12:19).
  - `624` — sessão do ESP (iniciado 23:21:14); `shr_probe.dll`, `shr_esp_probe*.dll`, `shr_bone_probe.dll` e `shr_esp.dll` injetados somente para leitura.
- O PID muda a cada inicialização; use sempre o PID atual.
- Versão: `alfa-0.1-19` / build 19, offline com bots.

## Ferramentas

Pasta: `C:\Users\Melinsky\Desktop\HerroDll\shr_tools`

| Arquivo | Função |
|---|---|
| `shr_common.h` | helpers de `VirtualQueryEx` / `ReadProcessMemory` / `WriteProcessMemory`. |
| `shr_tool.exe` | CLI externa: `pid`, `modules`, `regions`, `value`, `rescan`, `ptrmap`, `ptrpath`, `rootpath`, `aob`, `modfind`, `read`, `write`, `inject`. |
| `shr_dump.cpp` / `shr_dump.dll` | dumper Mono: escreve `offsets_dump.txt`. |
| `shr_live.cpp` / `shr_live.dll` | **resolvedor ao vivo, somente leitura**: escreve `live_pointers.txt` com ponteiros reais resolvidos pela metadata do Mono. |
| `shr_probe.cpp` / `shr_probe.dll` | sonda de câmera/classes/matrizes (somente leitura) → `probe_dump.txt`. |
| `shr_esp_probe.cpp` / `shr_esp_probe*.dll` | sonda da lista `Combatant::All`, nomes/HP/posições e ossos → `esp_probe*.txt`. |
| `shr_bone_probe.cpp` / `shr_bone_probe.dll`, `shr_verify*.dll` | sonda de ossos via `Animator.GetBoneTransform` e matrizes de câmera → `bone_probe.txt` / `verify_probe.txt`. |
| `shr_esp.cpp` / `shr_esp.dll` | **ESP com menu**: overlay caixa, esqueleto, HP e nome. Somente leitura. |
| `shr_dump2.dll`, `shr_dump3.dll`, `shr_dump4.dll` | versões intermediárias de debug; não usar. |

Build x64:

```bat
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\Melinsky\Desktop\HerroDll\shr_tools && cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- shr_tool.cpp /Fe:shr_tool.exe && cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_dump.cpp /Fe:shr_dump.dll && cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_live.cpp /Fe:shr_live.dll'
```

Build do ESP (precisa de `user32.lib` e `gdi32.lib`):

```bat
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d C:\Users\Melinsky\Desktop\HerroDll\shr_tools && cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /LD shr_esp.cpp /Fe:shr_esp.dll /link user32.lib gdi32.lib'
```

## O que já foi executado

1. `shr_dump.dll` injetado no PID `27320` dentro da partida:
   - `LoadLibraryW return: 0x715b0000`
   - `offsets_dump.txt` reescrito: 10.883.047 bytes, 127.891 linhas.
2. `ptrmap 27320`: 4.000.000 ponteiros, 1.435.321 alvos.
3. Leitura externa confirmou a cadeia de vida viva (valores abaixo).
4. `shr_live.dll` compilado e injetado no PID `6992` dentro da partida:
   - `LoadLibraryW return: 0x73ae0000`
   - `live_pointers.txt` gerado: 3.398 bytes, com ponteiros resolvidos via metadata do Mono.
   - Jogo continuou rodando normalmente após a injeção.

## Sessão final — resolvedor Mono ao vivo (PID 6992)

Arquivo: `C:\Users\Melinsky\AppData\Local\SuperHeroRoyale\jogo\.shr-versao-rVkyRE\live_pointers.txt`

```text
JogadorBR::<Local>  field_addr = 0x000001FC948DCFC0
                    resolved   = 0x000001FC3F5BC2C0

JogadorBR +0x20     combatant  = 0x000001FC9449D000
Combatant  +0xC8    isPlayer   = 1
Combatant  +0xCC    maxHp      = 200.0f
Combatant  +0xE0    Hp         = 200.0f
Combatant  +0xE4    Kills      = 0
```

Outros ponteiros resolvidos nessa sessão:

| Campo | Offset | Valor |
|---|---:|---:|
| `JogadorBR::inventory` | `+0x28` | `0x1FC94660000` |
| `JogadorBR::gun` | `+0x38` | `0x1FC943AC540` |
| `JogadorBR::controller` | `+0x40` | `0x1FC943AC7E0` |
| `JogadorBR::hero` | `+0x48` | `0x1FC6D479000` |
| `MundoVivo::_instancia` | estático | `0x1FC6D4A1B00` |
| `MundoVivo2::_inst` | estático | `0x1FC6D35CEA0` |
| `Combatant::All` | estático | `0x1FC68965960` |

`SalaRede::<Instancia>` e `VidaRede::<Local>` estavam nulos nessa sessão (jogo offline).

## Âncora estável dentro do módulo do jogo

Não existe um endereço fixo dentro de `Prototype.exe` que aponte para o objeto do jogador: o objeto vive no heap gerenciado pelo Mono. Mas existem globais estáveis dentro de `mono-2.0-bdwgc.dll` que guardam o `MonoDomain*`:

| Endereço relativo | Valor | Estabilidade |
|---|---|---|
| `mono-2.0-bdwgc.dll+0x73FEF8` | `0x1FB0CBE2D20` (`MonoDomain*`) | **Estável** — lido 2x com 3s de intervalo, igual |
| `mono-2.0-bdwgc.dll+0x73FF80` | `0x1FB0CBE2D20` (`MonoDomain*`) | **Estável** — idem |

Base do módulo nesta sessão: `0x00007FFDCC260000` (endereço absoluto muda; o `+0x73FEF8` não muda).

Achei também globais que **pareciam** âncora mas são voláteis (TLS/stack do Mono), por exemplo `mono-2.0-bdwgc.dll+0x73FD18` e `+0x75DEE8`: o valor mudou entre leituras. Não usar.

Para procurar esse tipo de global em outra versão:

```text
modfind <PID> "20 2D BE 0C FB 01 00 00"
```

...onde os bytes são o `MonoDomain*` atual em little-endian, tirado do `live_pointers.txt`.

Tentei também chegar ao vtable (`0x1FC938CD2E8`) e ao `static_base` (`0x1FC948DCFC0`) por corrente de ponteiros ancorada em módulo; **não há caminho direto**. O caminho correto e definitivo é:

```text
mono-2.0-bdwgc.dll+0x73FEF8  ->  MonoDomain*
mono_get_root_domain()      ->  MonoDomain*   (export oficial, mesma coisa)
MonoDomain + metadata       ->  Assembly-CSharp -> JogadorBR -> <Local>
```

É exatamente o que `shr_live.dll` faz. Ou seja: o “único lugar” estável é o global do módulo + a metadata do Mono; o endereço do objeto ainda é resolvido por sessão.

## ESP com menu (PID 624)

Arquivo: `C:\Users\Melinsky\Desktop\HerroDll\shr_tools\shr_esp.dll`

Injeção usada:

```text
inject 624 C:\Users\Melinsky\Desktop\HerroDll\shr_tools\shr_esp.dll
```

Resultado: `LoadLibraryW return: 0x95880000`. Janela `SHR_ESP_OVERLAY` criada dentro do processo; jogo `Responding = True`. O overlay é `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE` e usa `LWA_COLORKEY` com preto como transparente.

### Teclas do menu

| Tecla | Função |
|---|---|
| `F1` | ligar/desligar ESP |
| `F2` | caixa |
| `F3` | esqueleto |
| `F4` | barra de HP |
| `F5` | nome |
| `F6` | menu |

O menu aparece no canto superior esquerdo com o estado de cada opção.

### O que o ESP lê

- Câmera: `Camera.main` → `worldToCameraMatrix` + `projectionMatrix` (boxes/ossos projetados por sessão, sem endereço fixo).
- Inimigos: `Combatant::All` → `System.Collections.Generic.List<Combatant>` → `_items` (offset `0x10`) / `_size` (offset `0x18`); array com elementos em `+0x20 + i*8`. Na sessão havia 55 combatentes.
- Cada alvo: `displayName +0x20`, `aimPoint +0x28`, `headPoint +0x30`, `isPlayer +0xC8`, `maxHp +0xCC`, `<Hp>k__BackingField +0xE0`.
- Esqueleto: `JogadorBR::<Local>` → `hero +0x48` → `HeroAnimator::animator` → `Animator.GetBoneTransform(HumanBodyBones)` → `Transform.position`. 20 ossos (quadril, pernas, coluna, braços, mãos, cabeça).
- Projeção confirmada com a câmera/tiros ao vivo: alvo em `(420.714, 11.287, 33.661)` caiu em `(1077, 375)` numa tela `1360x768`; os demais ficaram fora da tela, como esperado.

Não há `WriteProcessMemory`, `VirtualAllocEx`, hooks ou suspensão de thread dentro do jogo. Toda a resolução é feita pela thread do próprio overlay com `mono_thread_attach`, somente leitura.

## Cadeia real confirmada dentro da partida (PID 27320)

Isto é ponteiro real resolvido, não scan temporário:

```text
Royale/Mono static  JogadorBR::<Local>k__BackingField
  field_addr = 0x0000019817528FC0
  valor      = 0x00000197C321C2C0   (objeto JogadorBR local)

JogadorBR +0x20  combatant
  addr  = 0x00000197C321C2E0
  valor = 0x0000019816FD7000       (objeto Combatant)

Combatant +0xE0  <Hp>k__BackingField
  addr  = 0x0000019816FD70E0
  valor = 200.0f
```

Leitura bruta ao vivo no `Combatant`:

| Endereço | Offset | Campo | Valor |
|---|---:|---|---:|
| `0x19816FD70C8` | `+0xC8` | `isPlayer` | `1` |
| `0x19816FD70CC` | `+0xCC` | `maxHp` | `200.0f` |
| `0x19816FD70D0` | `+0xD0` | `vestLevel` | `0` |
| `0x19816FD70D4` | `+0xD4` | `vestDurability` | `0.0f` |
| `0x19816FD70D8` | `+0xD8` | `helmetLevel` | `0` |
| `0x19816FD70DC` | `+0xDC` | `helmetDurability` | `0.0f` |
| `0x19816FD70E0` | `+0xE0` | `<Hp>k__BackingField` | `200.0f` |
| `0x19816FD70E4` | `+0xE4` | `<Kills>k__BackingField` | `0` |

Outros ponteiros lidos do `JogadorBR` local (`0x197C321C2C0`):

| Offset | Campo | Valor |
|---:|---|---:|
| `+0x20` | `combatant` | `0x19816FD7000` |
| `+0x28` | `inventory` | `0x19817223A10` |
| `+0x30` | `gloo` | `0x1981729C3C0` |
| `+0x38` | `gun` | `0x19817050540` |
| `+0x40` | `controller` | `0x198170507E0` |
| `+0x48` | `hero` | `0x197F10E9000` |
| `+0x50` | `camRig` | `0x1981709E540` |
| `+0x58` | `items` | `0x19873519D00` |
| `+0x11C` | `<IsDead>k__BackingField` | bool |

## Offsets estáveis da metadata

Esses offsets vêm do dump Mono e não mudam entre partidas. Os **endereços absolutos** mudam a cada inicialização; o que é estável é a cadeia simbólica `classe::campo` + offset.

### `.Combatant` — vida do jogador/bots

| Campo | Offset |
|---|---:|
| `displayName` | `+0x20` |
| `isPlayer` | `+0xC8` |
| `maxHp` | `+0xCC` |
| `vestLevel` | `+0xD0` |
| `vestDurability` | `+0xD4` |
| `helmetLevel` | `+0xD8` |
| `helmetDurability` | `+0xDC` |
| `<Hp>k__BackingField` | `+0xE0` |
| `<Kills>k__BackingField` | `+0xE4` |
| `<DeathTime>k__BackingField` | `+0xE8` |
| `<LastAttacker>k__BackingField` | `+0x38` |
| `<LastDamageTime>k__BackingField` | `+0xEC` |
| `<LastHpDamage>k__BackingField` | `+0xF0` |
| `<LastActualHpLost>k__BackingField` | `+0xF4` |
| `<LastResolvedDamage>k__BackingField` | `+0x40` |
| `<LastArmorAbsorbed>k__BackingField` | `+0xF8` |
| `Damaged` | `+0xB8` |
| `Died` | `+0xC0` |
| `escudo` | `+0x110` |
| `escudoDesde` | `+0x114` |
| `escudoAte` | `+0x118` |
| `escudoDecaiPct` | `+0x11C` |

Estáticos: `All +0x00`, `AnyDied +0x08`, `anyDamageResolved +0x10`, `ouvintesResolvido +0x18`, `ExtraBadaladas +0x20`, `PortaMp +0x28`, `resolvendo +0x30`.

### `.JogadorBR`

| Campo | Offset |
|---|---:|
| `combatant` | `+0x20` |
| `inventory` | `+0x28` |
| `gloo` | `+0x30` |
| `gun` | `+0x38` |
| `controller` | `+0x40` |
| `hero` | `+0x48` |
| `camRig` | `+0x50` |
| `items` | `+0x58` |
| `avisoNaoCabe` | `+0x60` |
| `avisoCabeca` | `+0x68` |
| `avisoResto` | `+0x70` |
| `erroClip` | `+0x78` |
| `estiloAvisoAntigo` | `+0x80` |
| `<NearestPickup>k__BackingField` | `+0x88` |
| `<PickupPromptText>k__BackingField` | `+0x90` |
| `<NearBox>k__BackingField` | `+0x98` |
| `<OpenBox>k__BackingField` | `+0xA0` |
| `rows` | `+0xA8` |
| `nearby` | `+0xB0` |
| `<Selected>k__BackingField` | `+0x120` |
| `<ListVersion>k__BackingField` | `+0x124` |
| `<IsDead>k__BackingField` | `+0x11C` |

Estático: `<Local>k__BackingField` —ponteiro para o `JogadorBR` local.

### `.Ficha` (outra representação de vida, usada em HUD/lógica antiga)

| Campo | Offset |
|---|---:|
| `Id` | `+0x54` |
| `Vida` | `+0x58` |
| `VidaMax` | `+0x5C` |
| `NivelColete` | `+0x60` |
| `NivelCapacete` | `+0x64` |
| `DurColete` | `+0x68` |
| `DurCapacete` | `+0x6C` |
| `Vivo` | `+0x70` |
| `Abates` | `+0x72` |
| `UltimoAtacante` | `+0x74` |
| `X` | `+0x80` |
| `Y` | `+0x84` |
| `Z` | `+0x88` |

### `Royale.Ponte.EstadoVida`

`Id +0x10`, `Versao +0x14`, `Vida +0x18`, `VidaMax +0x1C`, `Colete +0x20`, `Capacete +0x24`, `Vivo +0x28`, `Abates +0x2A`, `UltimoAtacante +0x2C`.

### `Royale.Multiplayer.CarroServidorMp`

`Tabela +0x10`, `Pos +0x38`, `hT +0x40`, `hX +0x48`, `hY +0x50`, `hZ +0x58`, `Id +0x60`, `Fase +0x62`, `Dono +0x64`, `Versao +0x80`, `Hp +0x84`, `ExplodeEm +0x88`, `TemPasso +0xB4`.

`RegrasCarrosMp`: `HpMax +0x78`, `VelMaxKmh +0x10`, `DanoTeto +0x88`, `AlcanceTiro +0x8C`, `CarrosMax +0xA4`.

### `.PlayerController`

`cameraPivot +0x20`, `cc +0x40`, `gun +0x50`, `body +0x58`, `weapon +0x60`, `walkSpeed +0x78`, `sprintSpeed +0x7C`, `crouchSpeed +0x80`, `proneSpeed +0x84`, `jumpHeight +0xA0`, `gravity +0xA4`, `planarVelocity +0x128`, `verticalVelocity +0x134`.

## Como obter ponteiro real sem travar/bugar o jogo

Abra o jogo, entre na partida e descubra o PID:

```text
pid Prototype.exe
```

Injete **somente o resolvedor** (leitura pura, sem `WriteProcessMemory` dentro do jogo e sem suspender threads):

```text
inject <PID> C:\Users\Melinsky\Desktop\HerroDll\shr_tools\shr_live.dll
```

Espere ~2s e leia:

```text
C:\Users\Melinsky\AppData\Local\SuperHeroRoyale\jogo\.shr-versao-rVkyRE\live_pointers.txt
```

Esse arquivo contém, para a sessão atual:

- `field_addr` do estático `JogadorBR::<Local>`;
- objeto `JogadorBR`;
- ponteiro `combatant` e todos os offsets/valores de `Combatant`;
- `raw` (leitura direta `ptr + offset`) e `value` (via `mono_field_get_value`) para conferir.

Se quiser atualizar o dump completo de metadata:

```text
inject <PID> C:\Users\Melinsky\Desktop\HerroDll\shr_tools\shr_dump.dll
```

Depois leia `offsets_dump.txt`. Evite `ptrmap`/`ptrpath` durante a partida: eles varrem milhões de endereços e não são necessários quando a metadata do Mono resolve a cadeia direto.

## Notas técnicas

- `mono_domain_get_assemblies` não existe nesse runtime; usa-se `mono_get_root_domain` + `mono_assembly_foreach`.
- `mono_domain_get()` sozinho retornou null na thread remota; o resolvedor usa `mono_get_root_domain()` e `mono_thread_attach()`.
- O dumper marca tipos com acesso inválido como `SKIP_CLASS access_violation`; isso é esperado e não invalida o dump.
- `??` é curinga em `aob`. Exemplo: `aob <PID> "48 8B 05 ?? ?? ?? ??"`.
- Endereços absolutos (`0x1981...`, `0x197C...`) são da sessão do PID `27320` e **não** devem ser reutilizados em outra partida. Use a cadeia simbólica + offsets.

## Versão final do ESP — caixa colorida, esqueleto nos inimigos, HP/combustível infinito (26/11/2026)

Fonte: `shr_esp.cpp`. DLL final testada: `shr_esp4.dll` (compilar com `/LD ... /Fe:shr_esp4.dll /link user32.lib gdi32.lib`).

### Controles

| Tecla | Função |
|---|---|
| `F1` | ligar/desligar ESP |
| `F2` | caixa (vermelha para inimigo, verde para jogador) |
| `F3` | esqueleto (vermelho nos inimigos, ciano no jogador local) |
| `F4` | barra de HP |
| `F5` | nome |
| `F6` | menu |
| `F7` | HP infinito (padrão: ON) |
| `F8` | combustível infinito (padrão: ON) |

### Correção das caixas brancas

`Rectangle()` da GDI preenche o interior com o brush atual (branco por padrão). O overlay agora seleciona `GetStockObject(NULL_BRUSH)` antes de desenhar caixas e juntas do esqueleto, e restaura o brush depois. Comparação de capturas: runs horizontais de branco (`esp_screen.png` → `esp_screen2.png`) caíram de 656 para 160.

### Esqueleto nos inimigos

`AutoSensacao::BonecoHerois` é um `String[]`, não uma lista de bots. A solução estável foi varrer somente o heap Mono (`0x1C000000000`–`0x1D000000000`) procurando o vtable de `BotAnimator` e ler os campos confirmados:

- `BotAnimator::animator` → `+0x20` (`UnityEngine.Animator`)
- `BotAnimator::combatant` → `+0x50` (`Combatant`)

Só se invoca `Animator.GetBoneTransform` quando o `combatant` tem o vtable de `Combatant` **e** aparece em `Combatant::All`, evitando chamar Mono em objeto inválido. O resultado é casado com o alvo correspondente e desenhado em vermelho. Varredura a cada ~150 ms, sem hooks e sem suspender threads.

### Escritas pontuais (vida/combustível)

- `JogadorBR::<Local>` → `combatant` → `<Hp>k__BackingField` (`+0xE0`) = `maxHp` (`+0xCC`), `float`.
- `Jetpack::<Local>` → `<Conta>k__BackingField` → `ContaJetpack::Combustivel` (`+0x28`) = `100.0f`, `float`.

As escritas usam `mono_field_get_offset` + `memcpy` protegido por SEH, direto no campo, sem hooks.

### Teste ao vivo (PID atual `624`, iniciado 25/11/2026 23:21:14)

- `shr_esp4.dll` injetado: `LoadLibraryW return: 0x27e0000`.
- Janela `SHR_ESP_OVERLAY` visível; jogo `Responding = True` após a injeção e após vários minutos.
- `esp_status.txt` (atualizado a cada 1s):

```text
targets=13 bot_candidates=225 vt_ok=215 target_ok=12 enemy_skeletons=12 enemy_bones=240 local_bones=20 hp_inf=1 fuel_inf=1
```

- Leitura externa confirmou `HP = 200/200` e `Combustivel = 100` durante o funcionamento do overlay.

Para reinjetar em outra sessão, use sempre o PID atual:

```text
inject <PID> C:\Users\Melinsky\Desktop\HerroDll\shr_tools\shr_esp4.dll
```

### Correção final — teclas não respondiam (26/11/2026)

Causa: o nome da classe da janela era fixo (`SHR_ESP_OVERLAY`). Como vários DLLs de teste foram injetados no mesmo processo, o `WndProc` da primeira DLL continuava desenhando; as teclas da DLL nova alteravam variáveis que não eram usadas no desenho.

Correções em `shr_esp.cpp`:

- A classe da janela agora é única por DLL: `SHR_ESP_OVERLAY_<endereço_do_módulo>` (ex.: `SHR_ESP_OVERLAY_00007FFE73AD0000`), via `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)`.
- `wc.hInstance` passou a ser o módulo da própria DLL.
- `F9` religa tudo de uma vez (útil porque todos os toggles começam ligados e pressionar a tecla desliga).
- `esp_status.txt` agora inclui `esp`, `box`, `skeleton`, `hp_bar`, `names`, `menu`, `hp_inf` e `fuel_inf`.

O processo foi reiniciado para remover as DLLs antigas. Sessão limpa:

- Novo PID: `24968` (iniciado 26/11/2026).
- Único módulo ESP carregado: `shr_esp6.dll` em `0x00007FFE73AD0000`.
- Única janela overlay: `SHR_ESP_OVERLAY_00007FFE73AD0000`.
- Jogo `Responding = True`.
- Antes de entrar na partida, `targets=0` e todos os toggles `=1` — ao entrar, os alvos e esqueletos aparecem automaticamente.

DLL final para uso: `shr_esp6.dll`.

```text
inject <PID> C:\Users\Melinsky\Desktop\HerroDll\shr_tools\shr_esp6.dll
```
