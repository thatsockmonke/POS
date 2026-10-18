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
`POS/Users/Owner/Saves/calculator`. Recent expressions are session-only.
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
Polynomials may have degree up to 8; equation solving supports degree 1/2, with
complex conjugate roots for quadratics. General symbolic trig functions, rational
functions with x in the denominator, multivariate algebra, arbitrary precision,
matrices and higher-degree root solvers are not implemented. Unsupported operations
fail explicitly. Numeric scientific functions can still operate on assigned x.

Calculations use finite IEEE double precision, with about 12 displayed significant
digits. Fraction output recovers a nearby rational with denominator ≤ 1,000,000;
it is not an unlimited exact-rational engine. Inputs are limited to 160 characters,
eight parser nesting levels and a 191-character answer; overlong symbolic output
is rejected instead of silently truncated. These bounds keep the device stack and
work space predictable. Native tests cover algebra, scientific domains, parsing,
independent arithmetic cases, actual keypad/text routing and saved-variable reopen.
Physical keypad/readability/stack measurements still need Cardputer verification.
