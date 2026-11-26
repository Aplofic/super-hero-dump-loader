# DUMP — metadata Mono, classes e ponteiros

## Alvo

- Jogo: `C:\Users\Melinsky\AppData\Local\SuperHeroRoyale\jogo\.shr-versao-rVkyRE\Prototype.exe`
- Engine: Unity **Mono** (`mono-2.0-bdwgc.dll`), **não** IL2CPP.
- Versão: `alfa-0.1-19` / build 19 — offline, contra bots.
- Dump completo: `dump/offsets_dump.txt` (~10 MB).

## Âncora estável do domínio

A forma confiável de chegar ao `MonoDomain*` é pelo módulo do Mono:

```text
mono-2.0-bdwgc.dll + 0x73FEF8  -> MonoDomain*
mono-2.0-bdwgc.dll + 0x73FF80  -> MonoDomain*
```

Alternativamente, chamar `mono_get_root_domain()`. `mono_domain_get()` **sozinho** retornou `null` quando a chamada vinha de uma thread remota; por isso as sondas chamam também `mono_thread_attach()`.

`mono_domain_get_assemblies` não existe nesse runtime; a enumeração é feita com `mono_assembly_foreach`.

## Como o dump foi gerado

1. Localizar `mono-2.0-bdwgc.dll` via `Module32First/Next` (ou `GetModuleHandle` dentro da DLL injetada).
2. Obter `MonoDomain*` pela âncora acima.
3. `mono_assembly_foreach` → iterar assemblies → classes → campos/propriedades/métodos.
4. Escrever `nome da classe`, `namespace`, `field offset`, `tipo`, etc. em `offsets_dump.txt`.

O dumper marca tipos inacessíveis como `SKIP_CLASS access_violation`; isso é esperado e não invalida o resto do dump.

## Classes e offsets confirmados

Todos os offsets são relativos ao início do objeto.

| Classe / campo | Offset | Observação |
|---|---|---|
| `Combatant::displayName` | `+0x20` | `System.String` |
| `Combatant::aimPoint` | `+0x28` | posição usada para a caixa |
| `Combatant::headPoint` | `+0x30` | topo da barra de HP |
| `Combatant::isPlayer` | `+0xC8` | bool |
| `Combatant::maxHp` | `+0xCC` | `float` (ex.: 200) |
| `Combatant::<Hp>k__BackingField` | `+0xE0` | `float`, alvo da escrita de HP infinito |
| `JogadorBR::combatant` | `+0x20` | ponteiro para `Combatant` |
| `JogadorBR::hero` | `+0x48` | ponteiro para o herói |
| `HeroAnimator::animator` | `+0x1C0` | `UnityEngine.Animator` do jogador |
| `BotAnimator::animator` | `+0x20` | `UnityEngine.Animator` do bot |
| `BotAnimator::combatant` | `+0x50` | `Combatant` do bot |
| `ContaJetpack::Combustivel` | `+0x28` | `float`, alvo da escrita de combustível |
| `Jetpack::<Local>k__BackingField` | — | instância local |
| `Jetpack::<Conta>k__BackingField` | — | leva a `ContaJetpack` |
| `Combatant::All` | — | lista estática com todos os combatantes vivos |

Propriedades usadas (via `mono_property_get_value`): `UnityEngine.Camera.main`, `worldToCameraMatrix`, `projectionMatrix`, `UnityEngine.Transform.position`, `Combatant.AimPosition`, `Combatant.HeadPosition`.

Método: `UnityEngine.Animator.GetBoneTransform(int)` — usado com 20 índices de ossos.

## Cadeias de ponteiro

```text
JogadorBR::<Local> -> combatante -> <Hp>k__BackingField (+0xE0) = maxHp (+0CC)

Jetpack::<Local> -> <Conta>k__BackingField -> ContaJetpack::Combustivel (+0x28) = 100.0f

JogadorBR::<Local> -> hero -> HeroAnimator::animator (+0x1C0) -> GetBoneTransform
BotAnimator (varrido no heap) -> animator (+0x20) -> GetBoneTransform
                              -> combatante (+0x50) -> validar em Combatant::All
```

## Sondas incluídas

| Arquivo | Saída | Função |
|---|---|---|
| `src/shr_dump.cpp` | `offsets_dump.txt` | metadata completa |
| `src/shr_live.cpp` | `live_pointers.txt` | resolve a cadeia do jogador dentro da partida |
| `src/shr_cheat_probe.cpp` | `cheat_probe.txt` | classes, offsets, vtables e caminhos de HP/combustível |
| `src/shr_bone_probe.cpp` | `bone_probe.txt` | índices/posições dos ossos |
| `src/shr_fuel_probe.cpp` | `cheat_probe.txt` | relê Jetpack/Conta/combustível |

Exemplo de saída real (sessão do PID `624`, **somente dessa sessão**):

```text
classes jetpack=000001C58572C910 conta=000001C58572DFB8 ...
JETPACK local=0x000001C7DCDEE2A0 conta=0x000001C7B624D680 fuel_type=System.Single fuel_off=40 fuel=100
LOCAL jogador=0x000001C677CA19A0 combatant=0x000001C7B5907780 hp_type=System.Single hp_off=224 hp=200/200
BOT_VTABLE=0x000001C62FFACDE8
```

## Armadilhas descobertas

- `AutoSensacao::BonecoHerois` **não** é uma lista de bots. É um `System.String[]` com `items=0x0` e `count=1`. Foi abandonado.
- `find_class_any("Transform")` retornou a classe errada. Use `find_class("Transform")` com namespace `UnityEngine`.
- `find_class("ContaJetpack")` exige procurar também o namespace `SuperHeroJetpack`.
- O heap Mono muda de endereço a cada partida. Na sessão do PID `624` ficava em `0x1C000000000`–`0x1D000000000`; em outra sessão apareceu em `0x166...`–`0x167...`. Por isso `shr_esp7.cpp` varre regiões dinamicamente.
- `??` é curinga no comando `aob` da CLI.
