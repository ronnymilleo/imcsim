# Architecture Plan: Continuous Behavioral Logic Models and Verilog Integration

This document outlines the strategic architecture for **Nodalys** to natively simulate digital logic parts using **Smooth Continuous / Behavioral MNA Macromodels**, and establishes the long-term blueprint for seamless co-simulation with **Verilog-A** (analog behavioral modeling) and **Verilog-HDL** (discrete event-driven digital simulation).

---

## 1. Executive Summary & Strategic Differentiation

### 1.1 Limitations of `ngspice` (XSPICE `digital.cm`)
In `ngspice`, digital components (`AND`, `OR`, `D_FLIP_FLOP`, etc.) are implemented via the XSPICE extension:
1. **Broken Linear Analyses:** Because XSPICE nodes live outside the Modified Nodal Analysis (MNA) matrix, any circuit containing a digital part fails or yields meaningless results in `.ac` (Bode frequency response), `.tf` (small-signal transfer function), and `.noise` (noise spectral density).
2. **DC Operating Point (`.op`) Convergence Stalls:** Discrete logic loops (e.g. cross-coupled latches or ring oscillators) have no static DC solution in a pure event simulator without analog feedback. `ngspice` frequently stalls or requires complex external workarounds (event-loop timeouts, forcing `uic` starting from $0\text{ V}$, etc.).
3. **Step Discontinuities & Numerical Ringing:** Hard step transitions in XSPICE d_to_a bridges inject high-frequency impulses into the analog integrator, forcing severe time-step cutbacks and occasional trapezoidal ringing.
4. **External Dependency:** Requires an external dynamic code model (`digital.cm`), complicating deployments on non-standard OS/architecture environments.

### 1.2 The `Nodalys` Paradigm: Smooth Continuous Behavioral Models in MNA
By compiling logic parts into native $C^1 / C^\infty$ differentiable MNA devices:
- **Works in ALL 6 Analyses:**
  - **`.op`**: Damped Newton-Raphson (with homotopy fallbacks) converges smoothly to the physical logic state.
  - **`.dc`**: Traces complete Voltage Transfer Curves (VTC: $V_{\text{out}}$ vs. $V_{\text{in}}$), natively revealing noise margins ($V_{IL}, V_{IH}, V_{OL}, V_{OH}$).
  - **`.tran`**: Continuous transitions without step singularities; adaptive Local Truncation Error (LTE) step control naturally accelerates through stable high/low rails and refines steps during transitions.
  - **`.ac` / Bode**: Evaluates small-signal frequency response, gate bandwidth, and loop stability in mixed-signal circuits (e.g. crystal oscillators, 555-timers, and logic buffers).
  - **`.tf`**: Measures input impedance $Z_{\text{in}}$ (virtually infinite for CMOS inputs), output drive resistance $R_{\text{out}}$, and small-signal gain $dV_{\text{out}}/dV_{\text{in}}$.
  - **`.noise`**: Propagates thermal and shot noise spectral density through buffers and gates.
- **Zero External Dependencies:** 100% clean-room C++23 built directly into `libnodalys.a`.
- **100% Native Coverage:** Unlocks the remaining 8 of 27 shipped `imcsim` examples that currently require `ngspice`.

---

## 2. Mathematical Architecture of Continuous Behavioral Logic Models

Each logic component (`LogicPart`) is compiled into a native `Device` consisting of four cascaded stages:

```
[Input Nodes v_1..v_n] 
       │
       ▼
┌────────────────────────┐
│  Input Sensing Stage   │  High impedance, Gmin regularized
└──────────┬─────────────┘
           │ u_i = (v_i - V_th) / ΔV_trans
           ▼
┌────────────────────────┐
│ Smooth Boolean Kernel  │  C^∞ sigmoidal continuous operators
└──────────┬─────────────┘
           │ f_logic ∈ (0, 1)
           ▼
┌────────────────────────┐
│  Dynamic RC Low-Pass   │  Inertial propagation delay (t_pd),
│     (d/dt, C_pd)       │  glitch rejection
└──────────┬─────────────┘
           │ v_internal
           ▼
┌────────────────────────┐
│ Thévenin Output Stage  │  Drive resistance R_out, V_out = v_internal
└──────────┬─────────────┘
           │
           ▼
[Output Node v_out]
```

### 2.1 Input Sensing Stage
- **High Impedance:** Draws zero DC current from the driving node, with a safety diagonal conductance $G_{\min} = 10^{-12}\text{ S}$ to ground preventing singular floating inputs.
- **Normalized Logic Metric:**
  Given nominal logic level $V_{\text{level}}$ (default $5.0\text{ V}$), $V_{IL} = 0.3 V_{\text{level}}$ and $V_{IH} = 0.7 V_{\text{level}}$:
  $$V_{\text{th}} = \frac{V_{IL} + V_{IH}}{2} = 0.5 V_{\text{level}}, \qquad \Delta V_{\text{trans}} = \frac{V_{IH} - V_{IL}}{2} = 0.2 V_{\text{level}}$$
  $$u_i = \frac{v_i - V_{\text{th}}}{\Delta V_{\text{trans}}}$$

### 2.2 Smooth $C^\infty$ Boolean Kernel
Instead of discrete `if/else` statements, continuous sigmoid functions based on $\tanh(u)$ are used:
- **Buffer & Inverter:**
  $$f_{\text{BUF}}(v) = \frac{1}{2}\left(1 + \tanh(u)\right) \in (0, 1)$$
  $$f_{\text{NOT}}(v) = 1 - f_{\text{BUF}}(v) = \frac{1}{2}\left(1 - \tanh(u)\right)$$
- **Multi-Input Combinational Gates (`AND`, `OR`, `NAND`, `NOR`, `XOR`, `XNOR`):**
  Using continuous probability products:
  $$\text{AND}(v_1, \dots, v_n) = \prod_{i=1}^n f_{\text{BUF}}(v_i)$$
  $$\text{OR}(v_1, \dots, v_n) = 1 - \prod_{i=1}^n \left(1 - f_{\text{BUF}}(v_i)\right)$$
  $$\text{XOR}(v_1, v_2) = f_1 + f_2 - 2 f_1 f_2$$
  $$\text{NAND} = 1 - \text{AND}, \qquad \text{NOR} = 1 - \text{OR}, \qquad \text{XNOR} = 1 - \text{XOR}$$

> [!NOTE]
> All analytical partial derivatives $\frac{\partial f}{\partial v_i}$ are continuous and non-zero across the transition region, guaranteeing that Newton-Raphson has well-conditioned Jacobians.

### 2.3 Dynamic Inertial Propagation Delay ($t_{\text{pd}}$)
In transient simulation, propagation delay is modeled via an internal dynamic node governed by:
$$\tau_{\text{pd}} \frac{d v_{\text{int}}}{dt} + v_{\text{int}} = V_{\text{level}} \cdot f_{\text{logic}}(v_1, \dots, v_n), \qquad \tau_{\text{pd}} = \frac{t_{\text{pd}}}{\ln 2}$$
- Discretized via the Trapezoidal rule into an MNA companion conductance $G_{\text{pd}} = \frac{2 C_{\text{pd}}}{h}$ and history current $I_{\text{eq}}$.
- **Inertial Glitch Rejection:** Pulses narrower than $t_{\text{pd}}$ naturally attenuate before reaching the output threshold, faithfully reproducing physical CMOS behavior without requiring ad-hoc discrete event queues.

### 2.4 Thévenin Output Stage
Coupled to the external output node through an output resistance $R_{\text{out}} \approx 50\text{ }\Omega$ to $100\text{ }\Omega$:
$$I_{\text{out}} = \frac{v_{\text{out}} - v_{\text{int}}}{R_{\text{out}}}$$
This allows gates to drive capacitors, resistive loads, pull-up/pull-down networks, and handle bus contention gracefully.

### 2.5 Sequential Logic (Flip-Flops and Latches)
For `D_FLIP_FLOP`, `JK_FLIP_FLOP`, `T_FLIP_FLOP`, and `SR_FLIP_FLOP`:
- Built as a master-slave cascade of continuous bistable latches.
- Smooth bistable feedback ensures that in DC operating point analysis (`.op`), the latch settles into a valid equilibrium state consistent with the clock and data inputs, with full support for user initial conditions (`uic`).

---

## 3. Blueprint for Future Verilog Integration

The architecture of `Nodalys` is designed so that behavioral logic models serve as the natural bridge toward full mixed-signal and Verilog support:

```
                                  ┌────────────────────────────────────┐
                                  │      imcsim Schematic / Netlist    │
                                  └─────────────────┬──────────────────┘
                                                    │
                   ┌────────────────────────────────┴────────────────────────────────┐
                   │                                                                 │
                   ▼                                                                 ▼
     [Native Behavioral Models]                                        [Future Verilog Modules]
    (Smooth C^1 gates in MNA today)                                                  │
                   │                                                                 │
                   │                     ┌───────────────────────────────────────────┤
                   │                     │                                           │
                   ▼                     ▼                                           ▼
       ┌───────────────────────┐ ┌───────────────────────┐       ┌───────────────────────────────────────┐
       │   Verilog-A (OSDI)    │ │  A2D / D2A Bridges    │ <───> │   Verilog-HDL / Digital Event Engine  │
       │  (Compact Analog DAE) │ │  (Mixed Boundaries)   │       │   (Event-Driven, Verilator / Native)  │
       └───────────┬───────────┘ └───────────┬───────────┘       └───────────────────┬───────────────────┘
                   │                         │                                       │
                   └────────────┬────────────┘                                       │
                                │                                                    │
                                ▼                                                    ▼
                   ┌───────────────────────────┐                         ┌───────────────────────┐
                   │   Nodalys MNA Engine      │ <── Breakpoints / ────> │ Digital Event Queue   │
                   │      (Continuous)         │     Synchronization     │    (Delta-Cycles)     │
                   └───────────────────────────┘                         └───────────────────────┘
```

### 3.1 Track 1: Verilog-A / Verilog-AMS (Compact Analog Device Models)
- **Role:** Industry-standard language (IEEE 1800 / Compact Model Coalition) for writing compact physical device models (e.g., BSIM, EKV, PSP, memristors, and behavioral analog blocks).
- **Compilation Path:** Modern compilers like **OpenVAF** compile Verilog-A into the standard **OSDI** (Open Simulation Device Interface) C ABI, exposing:
  $$\text{Evaluate}(v, \dots) \longrightarrow \{I, G, Q, C\}$$
- **Alignment with Nodalys `Device` Interface:**
  The `Device` abstraction in Nodalys ([`devices.h`](../../app-core/native/devices.h)) already uses this exact separation:
  - `Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time)`
  - `StampAc(MnaSystem &sys, const std::vector<double> &x_dc)`
  - `OutputNoiseVariance(...)`
  Consequently, when an OSDI/Verilog-A loader is introduced, compiled Verilog-A models will plug directly into `CompiledCircuit::Devices` with zero changes to the core MNA solver.

### 3.2 Track 2: Verilog-HDL / Digital Co-Simulation (FPGA & Digital Subsystems)
- **Role:** High-speed event-driven execution of large digital designs (e.g. synthesizable Verilog modules, UART, SPI, RISC-V cores) where continuous differential equations are too slow.
- **Architectural Elements:**
  1. **Dual-Island Partitioning:**
     - **Analog Island (Nodalys MNA):** Solves analog circuitry and behavioral gates.
     - **Digital Island (Verilog Engine):** Solves discrete event queues (e.g., via embedded Verilator C++ classes or a native event scheduler).
  2. **Boundary Interface Bridges (A2D & D2A):**
     - **D2A (Digital-to-Analog Bridge):** Takes discrete digital transitions $\{0, 1\}$ and uses the **Thévenin output stage** of our behavioral logic model to generate continuous $C^1$ ramps with finite rise/fall times and drive impedance $R_{\text{out}}$.
     - **A2D (Analog-to-Digital Bridge):** Uses the **input sensing stage** to detect threshold crossings ($V_{\text{th}} \pm V_{\text{hys}}$) and inject discrete events into the digital event queue.
  3. **Lock-Step Synchronization via Breakpoints:**
     - `Nodalys`'s transient engine ([`transient_analysis.cpp`](../../app-core/native/transient_analysis.cpp)) already features native **Breakpoint Registration**.
     - Whenever the digital engine schedules an event at timestamp $t_{\text{event}}$, it registers $t_{\text{event}}$ as an analog breakpoint.
     - The analog integrator advances smoothly up to $t_{\text{event}}$, resolves the step, evaluates A2D comparators, and hands execution to the digital event loop.

---

## 4. Phased Implementation Roadmap

```mermaid
flowchart TD
    subgraph Phase 1: Combinational Logic
        A[LogicGateDevice in Nodalys] --> B[Compiler integration for 8 gate types]
        B --> C[Verify logic_gates, adders, mux examples]
    end

    subgraph Phase 2: Sequential Logic
        D[FlipFlopDevice with Master-Slave Latches] --> E[Bistable feedback & uic support]
        E --> F[Verify d_flip_flop, counters examples]
    end

    subgraph Phase 3: Capabilities & 100% Native Coverage
        G[Remove DIGITAL_REASON in NativeBackend] --> H[100% of 27 imcsim examples run on Nodalys]
        H --> I[Add VTC DC sweep & TruthTable tests]
    end

    subgraph Phase 4: Future Verilog Co-Simulation
        J[A2D / D2A Bridge Pins] --> K[Verilator / Event-Driven Interface]
        K --> L[Verilog-A OSDI Device Loader]
    end

    Phase 1 --> Phase 2 --> Phase 3 --> Phase 4
```

| Phase | Milestone | Deliverables |
| :---: | :--- | :--- |
| **Phase 1** | **Smooth Combinational Gates** | `LogicGateDevice` (`AND`, `OR`, `NOT`, `NAND`, `NOR`, `XOR`, `XNOR`, `BUFFER`). Parameterized $V_{\text{level}}, t_{\text{pd}}$. Enables native compilation of `logic_gates.imcsim`, `half_adder.imcsim`, `full_adder.imcsim`, `multiplexer.imcsim`. |
| **Phase 2** | **Smooth Sequential Logic** | `FlipFlopDevice` (`D_FLIP_FLOP`, `JK_FLIP_FLOP`, `T_FLIP_FLOP`, `SR_FLIP_FLOP`). Master-slave continuous latches with smooth bistability and `uic` initialization. Enables `d_flip_flop.imcsim`, `counter.imcsim`. |
| **Phase 3** | **100% Native Engine Coverage** | Remove `DIGITAL_REASON` from `NativeBackend`. Complete native coverage across all 27 shipped schematics. Full `.dc` VTC noise margin tests and native `TruthTable`. |
| **Phase 4** | **Verilog & Mixed-Signal Integration** | Formalize A2D / D2A bridge pins. Breakpoint-driven lock-step co-simulation harness with Verilator / OSDI. |
