# Handoff — Backend nativo (`nodebase`) — 2026-11-09

## Contexto do projeto
O `imcsim` possui um backend de simulação nativo clean-room (Fase 1: Operating Point + DC Sweep +
Transient; AC sweep/noise ficam para fases futuras). O usuário escolhe o backend na GUI
(preferência `SimulationBackend` em `preferences.json`, combo na `PreferencesWindow`). A biblioteca
nativa chama-se `nodebase` (alvo CMake `nodebase`, gera `libnodebase.a`; diretório `app-core/native/`
e namespace `Core::Native`).

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
   - A antiga contagem em duas passadas (`CountCircuit` / `CountEntry`) foi **eliminada**.
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
   - **`BjtDevice`**: Modelo Gummel-Poon reduzido alinhado ao `bjtload.c` do SPICE
     ($q_b = \frac{q_1}{2}(1 + \sqrt{1 + 4 q_2})$), incluindo as correntes capacitivas das junções BE e BC
     em `TerminalCurrent()`.
   - **`MosfetDevice`**: Modelo Shichman-Hodges nível 1 com inversão simétrica Source/Drain (`d_eff`, `s_eff`)
     quando $V_{DS,\text{eff}} < 0$, diodo de corpo com anodo/catodo explícitos (`body_anode`, `body_cathode`),
     e correntes de capacitância de overlap (`CGSO`, `CGDO`) incluídas em `TerminalCurrent()`.

4. **Solver Newton-Raphson ([`newton.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/newton.h), [`newton.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/newton.cpp)) e Análises ([`analyses.h`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/analyses.h), [`analyses.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/app-core/native/analyses.cpp))**
   - `RunNewton` executa os 5 passos clássicos (Linearizar/Estampar → Resolver LU → Limiting `PNJLIM` →
     Teste de delta $\Delta x$ com `0.1 * RelTol` para nós → Verificação de resíduo KCL), com fallbacks
     automáticos para *gmin stepping* e *source stepping*.
   - `ComputeTransient` integra com Backward Euler no 1º passo e regra Trapezoidal nos passos seguintes,
     controle adaptativo de passo por erro local de truncamento (LTE) nos nós capacitivos e interpolação
     linear de tensões e correntes na grade de saída (`TransientCollector::AppendSegment`).

---

## Estado dos Testes
- **Paridade dual-backend ([`tests/backend_comparison_tests.cpp`](file:///mnt/DevDrive/Develop/Projects/imcsim-new_simulation_backend/tests/backend_comparison_tests.cpp))**: **12/12 casos passando**
  (incluindo `CheckOperatingPointParity`, `CheckDCSweepParity` e `CheckTransientParity`).
- **Suíte completa (`imcsim-tests`)**: todos os testes unitários, não-lineares, op-amps, chaves, fontes
  controladas e paridade passando.
