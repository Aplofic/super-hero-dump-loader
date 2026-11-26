# Anti-cheat e proteção do jogo — guia completo (educacional)

Este documento inverte a perspectiva do resto do repositório: **como proteger um jogo contra exatamente as técnicas demonstradas aqui**. É material de estudo defensor. O alvo deste projeto é um build offline; usar técnicas parecidas para burlar jogos online normalmente viola os termos de serviço.

## 1. Modelo de ameaça — o que este projeto demonstra

| Vetor | Sinal observável | O que o atacante ganha |
|---|---|---|
| `CreateRemoteThread` + `LoadLibraryW` | Thread remota terminando em `LoadLibraryW` | Executar código dentro do processo |
| Módulo `shr_esp*.dll` | Nome suspeito na lista de módulos | Persistência da sessão de teste |
| Thread de DLL não assinada | `StartAddress` fora dos módulos do jogo | Loop de ESP/godmode |
| Janela `SHR_ESP_OVERLAY_*` | `EnumWindows` + `WS_EX_LAYERED \| WS_EX_TRANSPARENT \| WS_EX_TOPMOST` | Ver inimigos através de paredes |
| `GetAsyncKeyState(VK_F1..F9)` | Leitura de teclas de função fora do loop do jogo | Alternar recursos |
| `mono_get_root_domain` / `mono_assembly_foreach` / `mono_field_get_value` | Uso de exports do Mono por módulo estranho | Descobrir classes, offsets e objetos |
| `Animator.GetBoneTransform` reflecionado | Invocação fora do código do jogo | Esqueleto dos bots |
| `VirtualQuery` + leitura 8 em 8 de heap | Padrão de acesso regular em `PAGE_READWRITE` | Encontrar `BotAnimator` |
| Escrita em `<Hp>` / `Combustivel` | HP = `maxHp` contínuo, `fuel = 100.0f` sob consumo | Vida e combustível infinitos |
| `esp_status.txt` | Artefato em disco criado pelo próprio processo | Diagnóstico do atacante |

## 2. Verdade fundamental

> **O cliente nunca é confiável.** Se o atacante controla a máquina, qualquer verificação rodando no mesmo processo pode ser contornada. Proteção "completa" é impossível nesse modelo.

O objetivo realista é combinar duas coisas:

1. **Tornar o ganho inútil** — a lógica importante é validada fora do processo controlado (autoridade).
2. **Aumentar o custo** — camadas de integridade e heurística que detectam ou dificultam a modificação.

A ordem de prioridade abaixo vai justamente nesse sentido. Camadas de detecção sozinhas são sempre contornáveis; autoridade de estado é a única barreira real.

---

## 3. Camada 1 — Autoridade e lógica de jogo (P0)

É a mais importante e a que menos depende de detecção.

- **Estado autoritativo**: mesmo em jogo offline, mantenha HP, dano, consumo de combustível, cooldowns e progressão em um único módulo de simulação, com uma cópia espelho protegida. A UI e o renderizador apenas leem essa cópia.
- **Invariantes rígidos**: `0 <= hp <= maxHp`, `0 <= fuel <= 100`, `dano >= 0`, `recarga dentro do cooldown`. Qualquer valor fora disso é rejeitado e registrado, não aceito.
- **Eventos assinados**: cada evento de dano/cura/consumo carrega `id` monotonicamente crescente e um nonce. A alteração direta de um campo não gera evento, então a divergência aparece.
- **Simulação determinística ou com checkpoint**: periodicamente recompute o estado esperado a partir dos eventos e compare com o estado atual. Se divergir, há manipulação.
- **Validação de consequência**: se `hp == maxHp` mas o jogador levou tiro, ou se `fuel` não caiu durante uso do jetpack, sinalize.
- **Servidor autoritativo (quando houver online):** nunca confie em HP/combustível enviados pelo cliente; envie apenas intenção (atirar, usar jetpack) e receba o resultado.

## 4. Camada 2 — Integridade de código e módulos (P0/P1)

- **Whitelist de módulos**: ao iniciar, registrar a lista legítima de DLLs e monitorar `LdrLoadDll` / `LoadLibraryW`. Módulo novo e não assinado => bloquear ou sinalizar.
- **Assinatura e hash de módulos**: verificar assinatura Authenticode e hash das seções executáveis na inicialização e periodicamente.
- **Integridade de `.text` e IAT/EAT**: comparar hash da seção de código e das tabelas de importação/exportação com o valor esperado do build.
- **Origem das threads**: cada thread do processo deve ter `StartAddress` dentro de um módulo da whitelist. Thread criada por DLL injetada tem endereço fora dela — este projeto é detectável por esse critério sozinho.
- **Proteção de processo**: onde o sistema permitir, usar *Protected Process Light*, *Arbitrary Code Guard* e políticas de *Code Integrity* para impedir carregamento de DLL não assinada.
- **Callbacks de kernel (somente se justificado):** `PsSetCreateThreadNotifyRoutine`, `PsSetCreateProcessNotifyRoutine` e `ObRegisterCallbacks` detectam injeção de fora do processo. Driver de kernel tem custo alto de compatibilidade, privacidade e segurança — não coloque em produção sem necessidade real.

## 5. Camada 3 — Integridade da memória de estado (P1)

- **Cópia espelho + hash com nonce**: manter uma segunda cópia de `Combatant`, `Jetpack` e `ContaJetpack` em memória protegida, recalculando `hash(campos || nonce)` em intervalos aleatórios. Escrita pontual como a deste projeto é detectada na próxima checagem.
- **Canários**: campos sentinela logo antes/depois de estruturas críticas; mudança de valor indica escrita fora do padrão.
- **PAGE_GUARD**: colocar páginas guard em torno de estruturas sensíveis para transformar escrita em exceção monitorável.
- **Validação cruzada**: `hp` deve bater com o último evento de dano/cura; `combustivel` com o tempo de uso do jetpack e a taxa de consumo.
- **Hash de vtables e metadata Mono**: tabelas de vtable e ponteiros de classe são estáveis por build; comparar com uma lista de hashes esperada detecta tanto tampering quanto uso de reflection para localizar objetos.

## 6. Camada 4 — Endurecimento específico do Mono (P1/P2)

- **IL2CPP** ou strip de metadata: remove os exports `mono_*` e torna a reflexão deste projeto impossível. É a mudança isolada mais efetiva contra este repositório.
- **Renomear/ofuscar tipos e campos no build de distribuição**: `Combatant::All`, `JogadorBR::<Local>`, `ContaJetpack::Combustivel` deixam de existir com esses nomes.
- **Layout aleatório por build**: reordenar campos e trocar offsets a cada compilação quebra dumps reutilizados.
- **Trocar a âncora do domínio**: `mono-2.0-bdwgc.dll+0x73FEF8` é estável nesta versão; em build novo, mude o padrão de acesso ao `MonoDomain`.
- **Restringir reflection**: monitorar quem chama `mono_runtime_invoke`, `mono_field_get_value`, `mono_property_get_value` e `Animator.GetBoneTransform`. Chamadas originadas de módulos não listados são suspeitas.
- **Não deixar lista global óbvia**: `Combatant::All` é um ponto único de descoberta de alvos; usar containers internos com nomes diferentes e acesso indireto.

## 7. Camada 5 — Overlay e input (P2)

- **Enumerar as próprias janelas** (`EnumWindows` filtrando pelo PID) e checar estilos `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST` com classe desconhecida. O overlay deste projeto aparece como `SHR_ESP_OVERLAY_<módulo>`.
- **Monitorar z-order/DWM**: janelas topmost transparentes criadas pelo próprio processo são um sinal forte.
- **Input pelo loop de mensagens**: usar `GetMessage`/`WM_KEYDOWN` em vez de `GetAsyncKeyState` em thread separada; detectar leitura de VK de função fora do loop legítimo.
- **Validar origem de input**: distinguir teclado físico de `SendInput`/`keybd_event` injetados quando o jogo aceitar apenas eventos confiáveis.

Essas checagens sozinhas são fracas (um atacante pode simplesmente não criar janela), mas funcionam bem em pontuação combinada.

## 8. Camada 6 — Heurística de comportamento (P1)

Sinais fracos que, somados, viram detecção confiável:

- HP igual a `maxHp` por muito tempo, sem evento de cura correspondente.
- Combustível constante enquanto o jetpack está ativo.
- Leitura reflecionada de `Animator.GetBoneTransform` em alta frequência.
- `VirtualQuery` seguido de leitura sequencial de regiões `PAGE_READWRITE`.
- Thread com loop de ~33 ms que chama `InvalidateRect` em uma janela layered.
- Arquivo `esp_status.txt` (ou qualquer artefato) criado no diretório do jogo.

Use **pontuação com limite configurável** e nunca banimento automático por um único sinal. Heurística gera falso positivo; combine com pelo menos uma evidência de integridade (camadas 2–4).

## 9. Camada 7 — Resposta e operação (P1)

- **Registrar, não travar**: registrar o evento com contexto (módulo, thread, hash, timestamp) em vez de derrubar o jogo. Crash imediato facilita diagnóstico do atacante e gera reclamação de usuário legítimo.
- **Telemetria mínima e consentida**: coletar só o necessário; nada de dados pessoais.
- **Resposta graduada**: aviso → desabilitar recurso → encerrar sessão → banimento, com revisão humana nos degraus finais.
- **Canary builds**: versões com nomes de campos diferentes publicadas internamente; se um dump externo usar os nomes do canary, você sabe qual build vazou.
- **Correlação**: um módulo detectado hoje deve ser procurado em logs antigos para achar a origem da sessão.

## 10. Camada 8 — Processo de desenvolvimento (P0)

- Usar **este repositório como teste de regressão**: injetar `shr_esp7.dll` em um build de teste e verificar quantas camadas acionaram. A meta é que a camada 1 (autoridade) impeça o efeito mesmo que a 2–7 não acionem.
- Testes automatizados de integridade: hash de `.text`, lista de módulos, invariantes de HP/fuel, detecção de janela layered.
- Revisar o dump a cada build novo: offsets e nomes mudam; o anti-cheat precisa acompanhar.
- Não depender de segredo de implementação: nomes e offsets deste repo são públicos; a defesa precisa funcionar mesmo sabendo disso.

## 11. Ofuscação — notas educacionais

Ofuscação aumenta o custo de análise estática: renomear símbolos, cifrar strings, alterar fluxo de controle, empacotar código. É usada tanto por software legítimo quanto por malware e **não esconde comportamento em runtime** — um overlay transparente continua sendo uma janela, e uma escrita em HP continua sendo uma escrita.

Exemplo puramente didático (não é evasão):

```cpp
// String "Combustivel" codificada por XOR para não aparecer em claro no binário.
const char nome[] = { 'C'^0x5A, 'o'^0x5A, 'm'^0x5A, 'b'^0x5A, 'u'^0x5A,
                      's'^0x5A, 't'^0x5A, 'e'^0x5A, 'v'^0x5A, 'e'^0x5A, 'l'^0x5A, 0 };
std::string claro;
for (char c : nome) if (c) claro.push_back(c ^ 0x5A);
```

Do lado da **defesa**, observa-se: entropia alta em seções de dados, imports suspeitos, strings decodificadas em memória durante execução e padrões de fluxo incomuns. Análise estática e dinâmica continuam vendo o comportamento. Use ofuscação como *uma* camada, nunca como substituto de autoridade e integridade.

## 12. Resumo priorizado

| Prioridade | Ação | Detects / neutraliza |
|---|---|---|
| **P0** | Estado autoritativo + invariantes + eventos | HP/fuel infinito deixa de ter efeito |
| **P0** | Whitelist de módulos e origem das threads | Injeção por `LoadLibraryW`/`CreateRemoteThread` |
| **P0** | Teste de regressão com este repo | Garante que as camadas realmente funcionam |
| **P1** | Hash espelho dos campos críticos | Escrita pontual em memória |
| **P1** | IL2CPP / strip de metadata / nomes ofuscados | Toda a cadeia de reflection deste projeto |
| **P1** | Heurística combinada com pontuação | Padrões de scan, overlay e toggling |
| **P1** | Log + resposta graduada + revisão humana | Operação sem falsos positivos destrutivos |
| **P2** | Detecção de janela layered e input | Overlay visual e teclas |
| **P2** | PPL / ACG / Code Integrity (se aplicável) | Carregamento de DLL não assinada |
| **P2** | Driver kernel | Injeção externa, com custo de compatibilidade |

**Conclusão:** a proteção mais "completa" possível não é a mais agressiva, e sim a que torna o cheat **inútil** (autoridade de estado) e **caro** (integridade + heurística), com operação cuidadosa para não punir jogadores legítimos.
