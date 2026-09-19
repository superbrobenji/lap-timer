# Power of 10 — reviewer checklist

Use this when reviewing any change to the shipped on-device C (`components/core`, `components/app`,
`components/drivers`, `main`). It is the human half of the enforcement; the automated half is the
blocking CI (`tools/lint/power_of_10.py --fail-on-violation --enforce-fnptr`, `clang-tidy`,
`cppcheck`, and the pedantic compiler warnings). Authority: the design at
`docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` (refined-rule interpretations);
deviations: `docs/power-of-10-deviations.md`.

Scope note: `tools/**` and `test/**` are exempt from the ten rules but must stay zero-warning.
Vendored/managed/IDF code (`jsmn`, `test/unity`, `managed_components/**`, `$IDF_PATH`) is fully exempt.

- [ ] **Rule 1 — simple control flow.** No `goto`, `setjmp`/`longjmp`, no direct or indirect recursion.
- [ ] **Rule 2 — bounded loops.** Every loop has a statically evident upper bound; add an explicit
      iteration cap (with a `/* rule 2: ... */` note) where a bound is not already structural. A
      `for`-loop index is not modified inside the loop body.
- [ ] **Rule 3 — no dynamic memory after init.** No `malloc`/`calloc`/`realloc`/`free`/`strdup`
      outside one-time startup; buffers are static or task-owned.
- [ ] **Rule 4 — length.** No function over 60 code lines; no `if`/`else`/`for`/`while`/`switch`/`do`
      compound-statement body over 30 code lines (blank/comment/brace-only lines excluded). Split into
      named helpers, or register a justified deviation.
- [ ] **Rule 5 — assertions (refined N=2/M=20).** Every function of more than ~20 code lines carries
      ≥2 *meaningful* assertions (real anomalies: NULL params, index/count/range bounds, valid
      enum/state, key invariants). Functions ≤20 lines are exempt. No tautological/`assert(true)`
      padding. Assertions are side-effect-free, use `CORE_ASSERT_*`/`LT_ASSERT_*`, and never abort on
      target.
- [ ] **Rule 6 — smallest scope + linkage.** Objects declared at the innermost scope that suffices; a
      file-only object is file `static`; a function-only-persistent object is function-local `static`;
      a non-static function has a prototype in a shared header (`-Wmissing-prototypes`/
      `-Wmissing-declarations` enforce this).
- [ ] **Rule 7 — return values + parameters.** Every non-`void` call result is used or explicitly
      `(void)`-cast with intent; parameters are validated (folded into rule-5 assertions).
- [ ] **Rule 8 — restricted preprocessor.** No token paste (`##`) or `#`-stringisation-as-logic, no
      `#undef`, no function-like / multi-statement / logic macros in `.c` files (object-like constant
      `#define`s in a single `.c` are permitted). Conditional-compilation directives stay well under
      the header-file count.
- [ ] **Rule 9 — restricted pointers.** At most one level of dereference per expression; no dereference
      hidden behind a macro/typedef. No function pointer outside the deviation register; a new
      function pointer must survive the §2 burden of proof (a compliant alternative shown to fail /
      not be worth its cost) OR be justified by the refined-rule tractability clause and registered.
- [ ] **Rule 10 — zero warnings, multiple analysers.** Clean under `-Wall -Wextra -Werror -Wshadow`
      (+ `-Wconversion` non-fatal) and under `clang-tidy` + `cppcheck` + the custom linter.

**Behavior-preserving retrofits:** a change that only restructures *how* code is written (splitting a
function, adding assertions, reworking a callback) must keep the host tests, the byte-exact PBM UI
goldens, the replay fixtures, and on-device behavior identical — and a firmware change flashes to
confirm.
