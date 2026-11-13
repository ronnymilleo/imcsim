# Behavioral Sources

A behavioral source is the SPICE `B` element: a voltage or a current that follows a free expression of node voltages, branch currents and time. `ngspice` reads the expression itself; `Nodalys` parses it into a compact program ([`expression.h`](../../nodalys/include/nodalys/expression.h), [`src/expression/`](../../nodalys/src/expression)) and evaluates it, with its exact partial derivatives, at each Newton iterate. The devices are [`BehavioralVoltageDevice` and `BehavioralCurrentDevice`](../../nodalys/src/devices/behavioral_device.cpp), built with `CircuitBuilder::AddBehavioralVoltage()` and `AddBehavioralCurrent()`, or from a netlist line.

The language is the one of the `ngspice` `B` element, so a netlist means the same on both engines. What `ngspice` reads and `Nodalys` does not is refused with a message that names it, never evaluated as something close (section 4).

---

## 1. Devices

### 1.1 Netlist and API

```spice
B1 out 0 V = v(a) * v(b)        * a voltage source: v(out) - v(0) = v(a) * v(b)
B2 out 0 I = 1m * tanh(v(in) / 26m)
B3 x   0 V = -1k * i(Vsense)    * the current of a voltage source, which may be written later
```

`V=` or `I=` and the expression take the rest of the line, with spaces anywhere; a `;` or `$` starts a comment. The API takes the same text with the nodes as numbers: `v(3)`, `v(3, 5)`, `i(V1)`.

| Element | Equation | Reported current |
| :--- | :--- | :--- |
| `V=` (branch unknown $j$) | $v_a - v_b = F(x)$ | $j$, into the + terminal, as for any voltage source |
| `I=` | the current $F(x)$ leaves node $a$, goes through the source and enters node $b$ | $F(x)$, into the + terminal, as for an independent current source |

A `B` element inside a `.subckt` is refused by the netlist parser, since the nodes and sources an expression names cannot be renamed for each instance; `imcsim` compiles blocks itself and does not go through that path.

### 1.2 Inputs

The expression reads **voltages** (`v(n)` is the voltage of node `n`, `v(n, m)` the difference, with ground written `0`, `gnd` or `ground`) and **currents** (`i(V1)` is the branch current of a voltage source, positive into its + terminal). The same input written twice is one input, whatever the case. `i()` may name a source added after the line that reads it: the names are bound when the circuit is built. A name that is not a voltage source is refused by `Build()`: `B1: i(I1) must name a voltage source of the circuit`.

`imcsim` writes `V(NET)` as `v(node)`, and `I(R1)` as the current of a 0 V probe in series with that terminal of `R1`, the probes that `ngspice` uses too, and the compiler plans one probe per terminal however many sources read it.

### 1.3 Stamp

Let $F(x)$ be the expression and $F_k = \partial F / \partial x_k$ its derivative with respect to input $k$, which reads a node voltage difference or a branch current. At the iterate $x_0$ the expression is replaced by

$$F(x) \approx F(x_0) + \sum_k F_k\,(x_k - x_{0,k})$$

so the voltage source stamps, on its branch row, $v_a - v_b - \sum_k F_k x_k = F(x_0) - \sum_k F_k x_{0,k}$, and the current source stamps $+F_k$ on row $a$ and $-F_k$ on row $b$ with the constant $F(x_0) - \sum_k F_k x_{0,k}$ on the right. The Jacobian is exact, so Newton converges quadratically near the solution wherever $F$ is smooth.

---

## 2. Language

### 2.1 Grammar

From the lowest precedence to the highest; every binary level groups from the left, except the ternary, which groups to the right.

```text
expression := or [ "?" expression ":" expression ]
or         := and  { "||" and }
and        := equal { "&&" equal }
equal      := compare { ("==" | "!=" | "<>") compare }
compare    := sum { ("<" | ">" | "<=" | ">=") sum }
sum        := product { ("+" | "-") product }
product    := unary { ("*" | "/") unary }
unary      := ("-" | "+" | "!") unary | power
power      := operand { ("^" | "**") exponent }        // from the left: 2^3^2 = (2^3)^2 = 64
exponent   := ("-" | "+") exponent | operand           // 2^-1 = 0.5
operand    := number | "pi" | "e" | "time" | "temper"
            | "v(" node [ "," node ] ")" | "i(" source ")"
            | function "(" expression { "," expression } ")" | "(" expression ")"
```

A sign binds looser than the power, so `-2^2` is $-4$; `!` binds tighter than the arithmetic, so `!0+1` is 2; `&&` binds tighter than `||`. Names are not case-sensitive. A number takes the SPICE scale suffixes, `f p n u m k meg g t` and `mil`, with the letters after them ignored: `4.7k`, `1meg`, `10n`, `5V`, `1kohm`, `2mil`. `M` is milli, as in SPICE.

Comparisons and logic give 1 or 0; for a condition, any value but 0 is true. The ternary evaluates the branch it takes and none else, so `v(x) > 0 ? ln(v(x)) : 0` has no domain error where `v(x)` is negative.

### 2.2 Constants and variables

| Name | Value | Note |
| :--- | :--- | :--- |
| `pi`, `e` | 3.14159..., 2.71828... | |
| `time` | the instant of the transient point, in seconds | 0 in every other analysis. In a transient the source follows it; in the small-signal analyses it is 0, as in `ngspice`. A `.dc` of `ngspice` sets `time` to the previous point of the sweep, a defect of its sweep that `Nodalys` does not copy: it holds 0, as documented. |
| `temper` | 27 | The temperature in degrees Celsius. `Nodalys` simulates at 27 °C only, which is what `ngspice` reads without a `.temp`; a stepped temperature is refused before the run. |
| `hertz` | refused | `ngspice` gives the frequency of the AC sweep, and the small-signal gain then depends on it, which an AC sweep of `Nodalys` stamps once. |

### 2.3 Operators and functions

The values were taken from `ngspice` 47, running each on `B1 1 0 V=<expression>`. Where `ngspice` differs from the C library, the table says so.

| Name | Meaning | $\partial/\partial x$ |
| :--- | :--- | :--- |
| `+ - * /` | arithmetic | $1,\ 1$; $1,\ -1$; $y,\ x$; $1/y,\ -x/y^2$ |
| `x / 0` | $x \cdot 10^{32}$, and $0/0 = 0$ | $10^{12}$ for a non-zero $x$ |
| `-x`, `+x` | sign | $\mp 1$ |
| `x ^ y`, `x ** y` | power. For $x > 0$ the usual power; for $x < 0$ and an integer $y$ the usual power ($(-2)^3 = -8$); for $x < 0$ and a fractional $y$, **0** (a real number has none); $0^0 = 1$; $0^y = 0$ for $y > 0$; an error for $0^y$ with $y < 0$ | $y x^{y-1}$, and $x^y \ln x$ for $y$ when $x > 0$ |
| `pow(x, y)` | as `^`, except that $x = 0$ gives 0 for every $y$ | as `^` |
| `pwr(x, y)` | $\operatorname{sgn}(x)\,\lvert x\rvert^y$, so it is odd in $x$: $\operatorname{pwr}(-2, 2) = -4$; $\operatorname{pwr}(0, 0) = 1$; an error for $x = 0$, $y < 0$ | $y\lvert x\rvert^{y-1}$; 0 at $x = 0$ |
| `abs sqrt exp` | $\lvert x\rvert$, $\sqrt x$ (an error for $x < 0$), $e^x$ clamped at $10^{99}$ | $\operatorname{sgn} x$; $1/(2\sqrt x)$, $10^{12}$ at 0; $e^x$, 0 past the clamp |
| `ln`, `log` | the **natural** logarithm (`log` is `ln` in `ngspice`); $\ln 0 = -10^{99}$; an error for $x < 0$ | $1/x$, $10^{12}$ at 0 |
| `log10` | the decimal logarithm, the same rules | $1/(x \ln 10)$ |
| `sin cos tan` | radians | $\cos$, $-\sin$, $1 + \tan^2$ |
| `asin acos` | an error for $\lvert x\rvert > 1$ | $\pm 1/\sqrt{1 - x^2}$ |
| `atan` | | $1/(1 + x^2)$ |
| `sinh cosh tanh` | | $\cosh$, $\sinh$, $1 - \tanh^2$ |
| `asinh acosh atanh` | `acosh` is an error for $x < 1$, `atanh` for $\lvert x\rvert \ge 1$ | $1/\sqrt{x^2+1}$, $1/\sqrt{x^2-1}$, $1/(1-x^2)$ |
| `sgn` | $-1$, 0 or 1 | 0 |
| `u` | the unit step, 0 below 0, **0.5 at 0**, 1 above | 0 |
| `u2` | a ramp from 0 at $x \le 0$ to 1 at $x \ge 1$ | 1 inside, 0 outside, **0.5 on the corners** |
| `uramp` | $\max(x, 0)$ | 1 above 0, 0 below, **0.5 at 0** |
| `floor ceil` | | 0 |
| `nint` | the nearest integer, a half to the even one: `nint(2.5)` is 2, `nint(3.5)` is 4 | 0 |
| `min(x, y)`, `max(x, y)` | | the operand that wins; on a tie, $y$ |
| `< > <= >= == != <>` | 1 or 0 | 0 |
| `&& \|\|` `!` | logic on non-zero | 0 |
| `c ? a : b` | $a$ if $c \ne 0$, else $b$ | that of the branch taken |

At a corner, a function of one side or the other has no derivative; `abs`, `uramp`, `u2` and `pwr` take the values `ngspice` takes, which is the mean of the slopes of the two sides for the first three and 0 for `pwr` at 0. A tie of `min` or `max` takes the second operand, as `ngspice` does.

A value past $10^{99}$, a `*` that overflows and any `inf` or `nan` are an error, as `ngspice` stops with `out of range for *`.

---

## 3. Derivatives

### 3.1 Forward-mode automatic differentiation

The program is a flat list of nodes, each an operation on earlier ones ([`expression_program.h`](../../nodalys/src/expression/expression_program.h)). The evaluation walks it once from the root and gives each node its value and its derivative with respect to **every input**, by the chain rule:

$$\frac{\partial f(a, b)}{\partial x_k} = \frac{\partial f}{\partial a}\,\frac{\partial a}{\partial x_k} + \frac{\partial f}{\partial b}\,\frac{\partial b}{\partial x_k}$$

where the two partials of the operation are the right-hand column of the table above ([`expression_math.cpp`](../../nodalys/src/expression/expression_math.cpp)). An input is a leaf with derivative 1 for itself and 0 for the others.

Forward mode was chosen over a symbolic derivative because it is exact, needs no expression algebra to simplify, is one pass over a program of a few dozen nodes however many inputs there are (a behavioral source reads a handful), and handles the jumps and corners with no special case: the function of each node returns the slope of the side it is on. The ternary evaluates one branch and takes its derivative, so the derivative of a guarded expression is that of the side that is taken. The expression tests check every partial against a central finite difference.

### 3.2 No smoothing

The steps, the comparisons, `min`, `max`, `abs`, `floor` and the others keep their jumps and corners: the derivative is that of the side the iterate is on, and a result is never changed by smoothing. A circuit that then does not converge on `Nodalys` where `ngspice` converges is reported, not hidden: Newton on a function with a jump has no guarantee.

### 3.3 Slopes that are not finite

At a point where the true slope is infinite, as for $\sqrt x$ and $\ln x$ at 0, the first Newton iteration, which starts from every node at 0 V, still has to linearize. `ngspice` protects a division by zero with the factor $10^{32}$; `Nodalys` returns the value the same way ($1/0$ is $10^{32}$) but gives the *slope* as $10^{12}$, because its LU factorization refuses a pivot smaller than $10^{-30}$ of the largest entry of the matrix, so a slope of $10^{32}$ would make every conductance of the circuit look singular.

### 3.4 Domain errors

A value outside the domain, as `sqrt(-1)` or `ln(-2)`, stops `ngspice` with `out of range for sqrt`. In `Nodalys` the device stamps nothing useful, so the iterate cannot count as converged (`Device::GetFault()`), the solve goes on, and if it fails the message names the source and the value: `B1: sqrt(-4) is out of range`. A guard in a ternary avoids it.

---

## 4. What is refused

The text is read when the circuit is built, and `imcsim` checks it again before the run through `BackendCapabilities::FindPartProblem`, so the editor shows the reason and not a failed run.

| Written | Message | Why |
| :--- | :--- | :--- |
| `limit(x, a, b)` | `limit() is a Monte Carlo function of ngspice (...), not a clamp; clamp with min(max(x, low), high)` | `ngspice` reads `limit(nominal, deviation)` as its Monte Carlo function, the nominal value plus or minus the deviation; in the runs made `limit(5, 1)` gave 6, `limit(5, -1)` gave 4 and `limit(v(2), -1, 1)` of a 0.7 V node gave 2, none of them a clamp. Reading it as a clamp would mean something else on the other engine. |
| `atan2(y, x)` | `atan2() is not available ... use atan(y / x)` | `ngspice` stops with `no such function 'atan2'`. |
| `gauss agauss unif aunif rnd random exponential` | `<name>() draws random numbers, which Nodalys does not run in an expression` | The values would differ on every run and from `ngspice`. |
| `hertz` | `hertz, the frequency, is not supported ...` | Section 2.2. |
| `%`, `\`, `=`, `&`, `\|` | `"%" cannot be used here` | Not an operator of the `B` element. |
| any other name | `"foo" is not a function Nodalys knows` | |
| `min(1)`, `sin(1, 2)` | `min() takes 2 operands, not 1` | |
| `v()`, `v(a, b, c)`, `i(a, b)` | `v() takes one or two node names...` | |
| A harmonic balance of `time` | `Nodalys harmonic balance cannot run B1: its expression reads time` | The balance evaluates a nonlinear device at each sample of the state without its instant. |

---

## 5. Analyses

| Analysis | Behavioral source |
| :--- | :--- |
| Operating point, DC sweep | Newton with the exact Jacobian; `time` is 0. A sweep of the source a source reads works; the swept source must be an independent one. |
| Transient | `time` is the instant of the point. The corners of a `time` expression are not breakpoints: the step control sees them through the local error, as `ngspice` does. |
| AC, transfer function, sensitivity, pole-zero, S-parameters, noise | The Jacobian at the operating point, so the small-signal gain is the derivative of the expression there: a squarer biased at 2 V has a gain of 4, and at 0 V none. A behavioral source adds no noise. |
| Harmonic balance | A nonlinear device evaluated at each time sample, with the Jacobian of each; an expression that reads `time` is refused. |

---

## 6. Agreement with `ngspice`

The parity tests ([`backend_comparison_tests.cpp`](../../tests/backend_comparison_tests.cpp)) run 16 smooth expressions in both forms, voltage and current, through the operating point, the DC sweep, the AC sweep and the transient on both engines, 3 that read `time` (without the DC sweep), another 10 with jumps and corners (swept away from the jumps, where the two engines must take the same side), and one inside a block. A direct run of 22 expressions in both forms at an operating point agrees to the 8 digits the Nodalys command line prints (relative difference at most $3 \cdot 10^{-8}$). The behavioral example of `imcsim` is in the cross-validation of every example ([`engine_cross_validation_tests.cpp`](../../tests/engine_cross_validation_tests.cpp)).

The one difference that remains is the `time` of a `.dc`, section 2.2, where `Nodalys` follows the documented behavior.
