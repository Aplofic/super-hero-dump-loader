# Anti-cheat e ofuscação — notas educacionais

Este documento inverte a perspectiva: **como um anti-cheat detectaria este projeto** e o que mudar para tornar a detecção mais difícil *do lado da defesa*. É material de estudo, não um guia de evasão. O alvo aqui é um jogo offline; usar técnicas semelhantes em jogos online normalmente viola os termos de serviço.

## 1. Vetores de detecção

| Sinal | O que um anti-cheat veria |
|---|---|
| `CreateRemoteThread` + `LoadLibraryW` | Thread remota iniciada em `LoadLibraryW`; monitorável por callbacks de criação de thread/processo no kernel |
| Módulo `shr_esp*.dll` | Nome suspeito na lista de módulos do processo |
| Thread criada por DLL não assinada | `CreateThread` com `StartAddress` fora dos módulos conhecidos do jogo |
| Janela `SHR_ESP_OVERLAY_*` | `EnumWindows` encontra classe customizada com `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST` |
| `GetAsyncKeyState(VK_F1..F9)` | Leitura de teclas de função fora do loop de input do jogo |
| `mono_get_root_domain` / `mono_assembly_foreach` / `mono_field_get_value` | Uso de exports do Mono a partir de módulo estranho; detectável por integridade de IAT/hook nas funções Mono |
| Leitura de `Combatant::All` e `Animator.GetBoneTransform` | Padrão de leitura reflecionada de objetos do jogo |
| `VirtualQuery` + leitura sequencial de heap | Varredura de memória com padrão de acesso regular |
| Escrita em `<Hp>` / `Combustivel` | Valor de HP igual a `maxHp` continuamente, ou `fuel == 100.0` sob consumo |
| Arquivo `esp_status.txt` | Artefato em disco criado pelo próprio processo do jogo |

## 2. Melhorias defensivas (o que um anti-cheat deveria fazer)

1. **Autoridade central da lógica de estado.** Mesmo em jogo offline, calcular dano, consumo de combustível e validação de HP em um único módulo confiável, com checagem de limites (`hp <= maxHp`, `fuel <= 100`). Isso torna a escrita local ineficaz sem precisar de detecção.
2. **Integridade de campos críticos.** Hash periódico (com nonce) de `Combatant`, `Jetpack` e `ContaJetpack`; comparar com uma cópia somente-leitura. Detecta alteração pontual mesmo sem hook de escrita.
3. **Whitelist de módulos e assinatura.** Rejeitar `LoadLibrary` de DLLs não assinadas dentro do processo do jogo.
4. **Detecção de overlay.** Enumerar janelas do processo e checar estilos `WS_EX_TRANSPARENT` + `WS_EX_TOPMOST` + classe desconhecida; também observar mudanças no z-order.
5. **Monitorar uso de reflection do Mono.** Checar chamadas a `mono_field_get_value`, `mono_property_get_value` e `mono_runtime_invoke` originadas de módulos que não sejam o próprio jogo.
6. **Heurística de varredura.** Detectar sequências longas de `VirtualQuery`/`ReadProcessMemory` ou leitura 8-em-8 de regiões `PAGE_READWRITE`.
7. **Sanity checks de runtime.** `hp == maxHp` por muito tempo, `fuel` constante durante uso do jetpack, e sincronia impossível entre vida e eventos de dano.
8. **Telemetria de input.** Teclas de função repetitivas em padrão de toggling são um sinal fraco, mas útil em combinação com os itens acima.

## 3. Ofuscação — o que é, e por que não resolve

Ofuscação aumenta o custo de análise estática: renomear símbolos, cifrar strings, alterar fluxo de controle, empacotar código. É usada tanto por software legítimo quanto por malware; **por si só não esconde comportamento em runtime** — um overlay transparente continua sendo uma janela, e uma escrita em HP continua sendo uma escrita.

Exemplo puramente didático de ofuscação de string (não é evasão):

```cpp
// String "Combustivel" codificada por XOR para não aparecer em claro no binário.
const char nome[] = { 'C'^0x5A, 'o'^0x5A, 'm'^0x5A, 'b'^0x5A, 'u'^0x5A,
                      's'^0x5A, 't'^0x5A, 'e'^0x5A, 'v'^0x5A, 'e'^0x5A, 'l'^0x5A, 0 };
std::string claro;
for (char c : nome) if (c) claro.push_back(c ^ 0x5A);
```

Do lado da **defesa**, o que se observa é: entropia alta em seções de dados, imports suspeitos, strings decodificadas em memória durante a execução e padrões de controle de fluxo incomuns. Ferramentas de análise estática e dinâmica (disassembly, hooks de API, sandbox) continuam vendo o comportamento.

**Conclusão:** ofuscação é um tema legítimo de estudo, mas não substitui controles de integridade nem autoridade de servidor. Para este projeto — offline, local e educacional — ela não foi usada de propósito: o código permanece legível justamente para servir de documentação.
