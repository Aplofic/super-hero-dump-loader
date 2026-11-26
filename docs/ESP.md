# ESP — arquitetura do overlay

Fonte: `src/shr_esp.cpp`. DLLs: `bin/shr_esp6.dll` (validada dentro da partida) e `bin/shr_esp7.dll` (varredura de heap dinâmica).

## Ciclo de vida

1. `DllMain` → `DLL_PROCESS_ATTACH` → `DisableThreadLibraryCalls` → `CreateThread`.
2. A thread nova chama `mono_thread_attach`, resolve classes/campos/propriedades/métodos e cria a janela.
3. Loop a cada ~33 ms: ler teclas, aplicar godmode, atualizar câmera/alvos/ossos, invalidar a janela, escrever status e despachar mensagens.
4. `WM_DESTROY` → `PostQuitMessage` → `guarded_detach` → encerramento limpo.

Nada é hookado; nenhuma thread do jogo é suspensa.

## Janela

- `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE`, `WS_POPUP`, cobrindo a tela toda.
- Classe: `SHR_ESP_OVERLAY_<endereço_do_módulo>`, obtido com `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)`.
- **Por que classe única:** com várias DLLs de teste carregadas no mesmo processo, o `WndProc` da primeira continuava desenhando. As teclas da DLL nova alteravam variáveis que não eram usadas no desenho. A classe por módulo garante que cada versão desenhe o seu próprio estado.
- `wc.hInstance` também é o módulo da própria DLL.

## Entrada

`GetAsyncKeyState` com detecção de borda (estado atual vs. anterior), F1–F9. Todos os toggles começam `true`; `F9` religa tudo.

## Aquisição de alvos

`Combatant::All` é enumerado via Mono. Para cada combatante:

- `displayName` (`+0x20`) para o nome;
- `aimPoint` (`+0x28`) e `headPoint` (`+0x30`) para caixa e barra de HP;
- `isPlayer` (`+0xC8`) para separar o jogador local;
- `maxHp` (`+0xCC`) e `<Hp>` (`+0xE0`) para a barra.

A projeção mundo→tela usa `Camera.main.worldToCameraMatrix` e `Camera.projectionMatrix`.

## Desenho

- Caneta **vermelha** para inimigo, **lima** para jogador, **ciano** para o esqueleto do jogador local.
- `Rectangle()` preenche o interior com o brush atual (branco por padrão). O overlay seleciona `GetStockObject(NULL_BRUSH)` antes de desenhar caixas e juntas, e restaura depois. Isso corrigiu as caixas brancas.
- Esqueleto: 20 ossos via `Animator.GetBoneTransform(int)`, juntados com linhas; mesmo esquema de `NULL_BRUSH`.
- Comparação de capturas: runs horizontais de branco caíram de **656** para **160** entre `esp_screen.png` e `esp_screen2.png`.

## Esqueleto dos inimigos

`BotAnimator` não é alcançável a partir de uma lista estável. A solução é varrer regiões de heap:

1. `VirtualQuery` sobre as regiões `MEM_PRIVATE` / `MEM_COMMIT` / `PAGE_READWRITE`.
2. Procurar o vtable de `BotAnimator`.
3. Validar `animator` (`+0x20`) pelo vtable de `UnityEngine.Animator`.
4. Validar `combatant` (`+0x50`) pelo vtable de `Combatant` **e** presença em `Combatant::All`.
5. Só então chamar `GetBoneTransform`. Isso evita invocar Mono em objeto inválido.

`shr_esp6.dll` usava a faixa fixa `0x1C000000000`–`0x1D000000000` (válida só naquela sessão). `shr_esp7.dll` remove essa dependência e varre dinamicamente, com refresh a cada ~500 ms e no máximo 32 esqueletos.

## Escritas pontuais

Duas escritas, ambas `float` e protegidas por SEH (`__try`):

```text
JogadorBR::<Local> -> combatante -> <Hp>k__BackingField (+0xE0) = maxHp (+0xCC)
Jetpack::<Local> -> <Conta>k__BackingField -> ContaJetpack::Combustivel (+0x28) = 100.0f
```

Usam `mono_field_get_offset` + `memcpy`. Nenhuma outra escrita existe no projeto.

## Status

`esp_status.txt` no diretório do jogo, atualizado a cada 1 s:

```text
targets=13 bot_candidates=225 vt_ok=215 target_ok=12 enemy_skeletons=12 enemy_bones=240 local_bones=20 esp=1 box=1 skeleton=1 hp_bar=1 names=1 menu=1 hp_inf=1 fuel_inf=1
```

`scanned_mb` aparece na versão 7 para medir o custo da varredura.
