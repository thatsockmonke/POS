# Calculator

Open **Calculator** from the launcher. It works offline, with no GPS/radio or
Internet dependency. The launcher still runs one foreground app at a time.

## Quick start

Type an expression and press **Enter**:

| Input | Result |
| --- | --- |
| `1/3+1/6` | `1/2 = 0.5` |
| `sqrt(2)` | `1.41421356237` |
| `2^3^2` | `512` (powers associate right) |
| `-2^2` | `-4`; use `(-2)^2` for `4` |
| `sin(30)` | `1/2 = 0.5` in DEG mode |
| `a=12`, then `a/5` | `12/5 = 2.4` |
| `ans*2` | Twice the previous numeric answer |
| `5!`, `10%` | `120`, `1/10 = 0.1` |

DEG is the default. **Tab → Toggle DEG / RAD** changes the angle mode.
**Esc** or the global Home shortcut closes Calculator. Variables, angle mode
and Ans save on a normal close, using checksummed redundant app-save slots in
`POS/Users/Owner/Saves/calculator`. Recent expressions persist in a separate readable SD journal.
Corrupt/future saves are retained and not overwritten by a temporary session.

## Typing and buttons

In **typing mode**, punctuation such as `/`, `.`, and `,` enters expressions.
This matters because the Cardputer also prints arrow legends on those keys.
Backspace/Delete removes the last character. **Tab → Edit expression** opens the
normal editor-style input box for moving its cursor and editing the middle.

**Ctrl+K**, or **Tab → Toggle typing / keypad**, selects keypad mode. Bare arrow
legends move between the visible buttons; **Enter** presses the selected one.
Fn/USB arrow events also enter keypad mode. Digits typed directly return to typing
mode. The footer identifies the current mode. The grid includes numbers,
arithmetic, parentheses, powers, square root, Functions, Variables, CAS and Clear.

**Funcs** or **Tab → Scientific functions** opens grouped function buttons.
They insert a function name and `(`; type arguments and the closing `)`.
Use **Tab → View full answer** for answers longer than the main result area.

## Scientific functions

| Group | Methods |
| --- | --- |
| Trigonometry | `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)` |
| Hyperbolic | `sinh(x)`, `cosh(x)`, `tanh(x)`, `asinh(x)`, `acosh(x)`, `atanh(x)` |
| Logs/powers | `ln(x)`, `log(x)` (base 10), `exp(x)`, `sqrt(x)`, `cbrt(x)`, `root(x,n)`, `pow(x,y)`, `x^y` |
| Rounding | `abs(x)`, `floor(x)`, `ceil(x)`, `round(x)`, `trunc(x)` |
| Combinations/integers | `fact(n)` or `n!`, `ncr(n,r)`, `npr(n,r)`, `gcd(a,b)`, `lcm(a,b)`, `mod(a,b)` |
| Statistics/geometry | `min(a,b)`, `max(a,b)`, `hypot(a,b)`, `atan2(y,x)`, `mean(a,b,...)`, `std(a,b,...)` |
| Constants/conversion | `pi`, `tau`, `e`, `ans`, `rad(degrees)`, `deg(radians)` |
| Shortcuts | Square, cube, reciprocal, percentage, factorial, `1/` |

`std` is sample standard deviation (n−1); it needs at least two numbers.
Statistics accept up to 16 arguments. GCD/LCM require integers within the exact
integer range of a double; factorial/combinations/permutations accept n ≤ 170.
Hyperbolic functions do not use DEG/RAD. Invalid domains, division by zero and
non-finite results produce errors. `root(-27,3)` is −3; use odd roots explicitly
for negative radicands rather than a fractional power.

## Variables and computer algebra

**Vars** lists a–z. Choose a variable and enter a number or numeric expression.
Blank clears it. `e` remains Euler's constant. Assignments such as `a=12` also work.

**CAS** or **Tab → Computer algebra** offers:

- **Solve equation for x:** `2*x+3=7` → `x = 2`;
  `x^2-5*x+6=0` → `x = 3 or 2`; `x^2+1=0` → `x = 0 +/- 1*i`.
  An expression without `=` is treated as equal to zero.
- **Simplify / expand polynomial:** `(x+1)^3` → `x^3 + 3*x^2 + 3*x + 1`.
- **Differentiate in x:** `3*x^4-2*x+7` → `12*x^3 - 2`.
- **Integrate in x:** `3*x^2+2*x+1` → `x^3 + x^2 + x + C`.

CAS always treats **x** as symbolic, even if a numeric x is saved. Other variables
are substituted from their saved numbers. In ordinary evaluation, clear x in Vars
to make it symbolic again. Use `*` between named variables. Implicit products such
as `2x`, `2pi`, and `2(3+4)` also work.

This is a **bounded polynomial CAS**, not an unrestricted symbolic algebra engine.
Polynomials may have degree up to 8; equation solving supports degrees 1/2 (real/complex) and degrees 3–8 (numeric real roots). General symbolic trig functions, rational
functions with x in the denominator, multivariate algebra, arbitrary precision
and unrestricted symbolic systems are not implemented. Bounded matrices and higher-degree real root isolation are available in 0.8.0-dev. Unsupported operations
fail explicitly. Numeric scientific functions can still operate on assigned x.

Calculations use finite IEEE double precision, with about 12 displayed significant
digits. Fraction output recovers a nearby rational with denominator ≤ 1,000,000;
it is not an unlimited exact-rational engine. Inputs are limited to 160 characters,
eight parser nesting levels and a 191-character answer; overlong symbolic output
is rejected instead of silently truncated. These bounds keep the device stack and
work space predictable. Native tests cover algebra, scientific domains, parsing,
independent arithmetic cases, actual keypad/text routing and saved-variable reopen.
Physical keypad/readability/stack measurements still need Cardputer verification.

## 0.8.0-dev modes, graphs and worksheets

**Tab → More modes / worksheets** opens the new tools:

- **Exact fractions:** integer/decimal `+ - * / ^` and parentheses use checked
  signed 64-bit rational arithmetic. `0.1+0.2` returns exactly `3/10`.
  Overflow, zero division, excessive nesting and noninteger exponents fail
  explicitly. This mode is separate from the scientific double-precision mode.
- **Linear systems:** enter `2*x+y=3;x-y=0` for `x=1; y=1`. Use 2–4 equations
  in x,y,z,w, in that order. Coefficients can be decimals or simple fractions.
  Nonlinear products/functions are refused. Augmented rows are also accepted:
  `2,1,3;1,-1,0`. There must be a unique, reasonably conditioned solution.
- **Matrices / vectors:** rows use semicolons and entries use commas, e.g.
  `1,2;3,4`. Add, subtract, multiply, determinant and inverse support up to 4×4.
  Dot product takes two single-row vectors of equal length, up to four entries.
  Inversion uses scaled partial pivoting; singular/range failures are explicit.
- **Unit conversion:** the value can be a scientific expression using saved
  variables. Enter source/destination units separately. Supported units are
  `m km cm mm mi ft in yd`, `kg g lb oz`, `s min h`, `m/s km/h mph`,
  `m2 km2 acre`, `L mL gal` (US gallon), and `C F K`.
  Incompatible dimensions fail; temperatures use their affine offsets.
- **Programmer:** choose unsigned 8/16/32/64-bit width. Enter decimal, `0x` hex,
  `0b` binary (first operand), or leading-zero octal. One `& | ^ << >>` operation
  or unary `~` is supported. Results show decimal, padded hex and binary.
  Values wrap to the selected width. Negative/overflowing literals and shifts
  at least as wide as the selected word are rejected.
- **Graph y=f(x):** graphs the current numeric expression with x substituted,
  without changing saved variables or Ans. Arrows pan, `+/-` zoom, `0` resets,
  Escape returns. It uses a 2,640-byte monochrome raster; invalid samples and
  large jumps are not joined across the screen. DEG/RAD follows the calculator.
- **Save named worksheet:** writes a new `.calc` JSON file under
  `POS/Users/Owner/Documents`, with the expression, variables, Ans and angle mode.
  A temporary file is synced and CRC checked before publication. Existing names
  and system paths are protected. **Files → open a .calc file** reopens Calculator;
  **Open worksheet** also works from Calculator. Unsupported/corrupt worksheets
  remain untouched and leave the session unchanged.

**Recent expressions** now loads the last 16 complete expressions from a bounded
16-KiB tail of `POS/Users/Owner/calculator-history.jsonl`. Choose one to reuse it,
then use **Edit expression** to edit it. The complete readable journal remains on
SD. Its append path isolates partial records left by interrupted writes.

The existing polynomial CAS additionally isolates **numeric real roots of degrees
3–8**, including repeated roots, using derivative critical points and bisection.
Degree 1/2 retains its previous real/complex solver. Higher-degree complex roots,
general symbolic trig/rational functions and arbitrary precision remain outside
this bounded implementation; range and conditioning limits are reported.
