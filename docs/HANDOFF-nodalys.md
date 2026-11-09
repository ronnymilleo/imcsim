# Handoff — Backend nativo (`Nodalys`) — 2026-11-09

## Contexto do projeto
O `imcsim` possui o backend de simulação nativo clean-room **Nodalys** (contração de *nodal analysis* —
`nodal` + `-lys` de `analysis`, em referência à Análise Nodal Modificada no núcleo do motor).
A Fase 1 cobre Operating Point, DC Sweep e Transient; AC sweep, ruído e modelos digitais ficam para as
próximas fases. O usuário escolhe o backend na GUI (`"ngspice"` ou `"Nodalys"` na preferência
`SimulationBackend` em `preferences.json`, combo na `PreferencesWindow`).
O alvo CMake chama-se `nodalys` (gera `libnodalys.a`; código em `app-core/native/`, namespace
`Core::Native`).

### Como compilar e rodar os testes
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
XDG_RUNTIME_DIR=/tmp TMPDIR=/tmp cmake --build build -j$(nproc) --target imcsim-tests
./out/debug/bin/imcsim-tests
```
*(Nota: `XDG_RUNTIME_DIR=/tmp` garante que o passo pós-build `catch_discover_tests` do Catch2 consiga
gravar seu arquivo temporário mesmo dentro de sandboxes restritos).*

---

## Arquitetura Didática do Motor (`app-core/native/`)

A implementação foi organizada e documentada visualmente para facilitar leitura, manutenção e extensão
sem depender de IA:

1. **Sistema MNA Unificado ([`mna_system.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/mna_system.h), [`mna_system.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/mna_system.cpp)) e Solver Denso LU ([`dense_matrix.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/linear/dense_matrix.h), [`dense_matrix.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/linear/dense_matrix.cpp))**
   - Monta o sistema linear $A \cdot x = \text{rhs}$ onde cada incógnita é descrita por `IsNodeUnknown(index)`
     (`true` para tensões nodais regidas por KCL; `false` para correntes de ramos de tensão/indutores).
   - O nó terra (`0`) tem índice `-1` e é ignorado silenciosamente em `AddA()` e `AddRhs()`, eliminando
     `if`s de contorno nas estampas dos dispositivos.
   - `CheckResidual()` verifica o resíduo real $|A(x)\cdot x - \text{rhs}(x)|$ após a convergência de Newton.

2. **Compilador de Passada Única ([`compiler.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/compiler.h), [`compiler.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/compiler.cpp))**
   - O `Builder` percorre o circuito uma única vez (`AddCircuit` / `AddEntry`), alocando nós internos
     (`AllocateInternalNode()`) e ramos de corrente (`AllocateBranch()`) sob demanda no exato momento em
     que cada componente é instanciado.
   - O macromodelo Boyle de amplificadores operacionais (`AddBoyleOpAmp`) está dividido nos 5 blocos
     físicos do artigo original (par diferencial de entrada, estágio de ganho com compensação Miller,
     estágio de saída, limitador de curto-circuito e grampos de excursão positiva/negativa), com nomes de
     nós explícitos e correspondência linha a linha com `FormatOpAmpSubcircuit()` em [`op_amp.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/components/op_amp.cpp).

3. **Dispositivos Lineares e Não-Lineares ([`devices.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/devices.h), [`devices.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/devices.cpp), [`nonlinear.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/nonlinear.h), [`nonlinear.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/nonlinear.cpp), [`pn_junction.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/pn_junction.h))**
   - Todas as estampas básicas (condutância, fonte de corrente, ramo de tensão, equivalente de Norton
     `StampNortonBranch` para ramos não-lineares $I(v) \approx G_{\text{eq}} v + I_{\text{eq}}$ e modelo
     companheiro de carga `ChargeModel`) estão documentadas com diagramas no código.
   - **`DiodeDevice`**: Equação de Shockley contínua com ruptura reversa suave ($C^\infty$ em $v = -\text{BV}$)
     e joelho efetivo $\text{BV}_{\text{eff}} = \text{BV} - N V_T \ln(I_{\text{BV}}/I_S)$ para `PNJLIM`.
   - **`BjtDevice`**: Modelo Gummel-Poon reduzido alinhado ao `bjtload.c` do SPICE
     ($q_b = \frac{q_1}{2}(1 + \sqrt{1 + 4 q_2})$), incluindo as correntes capacitivas das junções BE e BC
     em `TerminalCurrent()`.
   - **`MosfetDevice`**: Modelo Shichman-Hodges nível 1 com inversão simétrica Source/Drain (`d_eff`, `s_eff`)
     quando $V_{DS,\text{eff}} < 0$, diodo de corpo com anodo/catodo explícitos (`body_anode`, `body_cathode`),
     cargas de overlap (`m_Cgs * m_Vgs`, `m_Cgd * m_Vgd`) e correntes capacitivas incluídas em `TerminalCurrent()`.

4. **Solver Newton-Raphson ([`newton.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/newton.h), [`newton.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/newton.cpp)) e Análises ([`analyses.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/analyses.h), [`analyses.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/analyses.cpp))**
   - `RunNewton` executa os 5 passos clássicos (Linearizar/Estampar → Resolver LU → Limiting `PNJLIM` →
     Teste de delta $\Delta x$ com `0.1 * RelTol` para nós → Verificação de resíduo KCL), com fallbacks
     automáticos para *gmin stepping* e *source stepping*.
   - `ComputeTransient` integra com Backward Euler no 1º passo e regra Trapezoidal nos passos seguintes,
     controle adaptativo de passo por erro local de truncamento de 3ª diferença dividida
     ($\text{LTE}_{\text{trap}} = \frac{1}{2} h^3 |d_{123}|$, Nagel SPICE2 Eq. 5.26) e interpolação linear
     de tensões e correntes na grade de saída (`TransientCollector::AppendSegment`).

---

## Validação e Estado dos Testes
1. **Camada 1 — Referências Analíticas e Goldens Python ([`tests/reference/`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/tests/reference/README.md), [`tests/native_backend_tests.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/tests/native_backend_tests.cpp))**:
   - Script `tests/reference/generate_goldens.py` e CSVs de referência analítica documentados em `tests/reference/README.md`.
2. **Camada 2 — Paridade Dual-Backend ([`tests/backend_comparison_tests.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/tests/backend_comparison_tests.cpp))**:
   - **12/12 circuitos passando** (`CheckOperatingPointParity`, `CheckDCSweepParity` e `CheckTransientParity`).
3. **Camada 3 — Todos os Exemplos Embarcados ([`tests/examples_tests.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/tests/examples_tests.cpp))**:
   - `Every example either runs on Nodalys or is flagged unsupported` verifica todos os 27 exemplos do projeto.
