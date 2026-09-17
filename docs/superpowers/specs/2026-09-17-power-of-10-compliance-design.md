# Power of 10 Compliance — Design

**Status:** design, pending user review. **Owning plan:** Plan 4.5 (inserted before Plan 5), tags `p4.5-dN` / `plan-4.5-done`.

**Goal:** Bring the shipped, on-device firmware C into strict compliance with the ten rules of the *Power of 10: Rules for Developing Safety-Critical Code* (G. Holzmann, JPL/NASA), and keep it compliant for all future work, so the lap timer stays statically analysable, bounded, and reliable.

**Related:** the binding product spec `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§17.9 already mandates `-Wall -Wextra -Werror -Wshadow`, no `malloc` after init, and the `CORE_ASSERT_*` hook — this design tightens and enforces the rest). The reference: https://web.eecs.umich.edu/~imarkov/10rules.pdf.

## 1. Scope

Binds (full Power of 10): the on-device C in `components/core/`, `components/app/`, `components/drivers/`, and `main/`.

Exempt from the ten rules but still must build zero-warning under the project flags (rule 10 only): the host-only code `tools/replay/**` and `test/**` (dev tools and Unity tests, which never run on the device and deliberately use forbidden patterns such as long fixtures).

Fully exempt (not our code): vendored / managed / SDK components — `test/unity`, `core/util/jsmn`, `joltwallet/littlefs`, NimBLE, and all of ESP-IDF.

Non-C generators (`tools/fonts/gen_fonts.py`, tracks/sim generators) are out of scope (Python).

## 2. Strictness model: strict, with an adversarially-justified deviation register

All ten rules are adopted as the standard for in-scope code. Compliance is the default; a deviation is a last resort. This mirrors how Power of 10 is applied in flight software, but with a hard burden of proof on every exception:

**The deviation burden of proof.** Before any site is registered as a deviation, the reviewer must first build the strongest possible argument that it should *not* be one — i.e. concretely how the code could be rearchitected to comply (the compliant alternative, spelled out, not hand-waved). A deviation is accepted only when (a) that compliance argument genuinely fails — the alternative is infeasible, or so costly/complex/risky that it is clearly not worth it — **and** (b) the deviation's benefit is worth the cost of carrying it: a permanent hole in the static-analysis guarantee, a maintained register entry, and the precedent it sets. When in doubt, comply. "It's how it's written now" and "it's convenient" are never sufficient — the retrofit will pay to comply wherever the argument for the exception does not clearly win.

Static analysis accepts exactly the registered sites and rejects any new, unregistered function pointer or other deviation, so the register stays small, explicit, and reviewed.

## 3. Rule-by-rule interpretation for this codebase

1. **Simple control flow** — no `goto`, `setjmp`/`longjmp`, or recursion in in-scope code (an initial scan finds no `goto`/`longjmp`; recursion is caught by `clang-tidy misc-no-recursion`). Deviations: none expected.
2. **Bounded loops** — every loop has a statically evident upper bound. Ring/queue drains are bounded by capacity; add an explicit iteration cap where a bound is not already structural (e.g. `ses` resync, `exp` `EXP_FULL` retry, byte-scan parsers). Full static provability is not fully automatable, so this rule is enforced by a review checklist item plus the cap convention, not solely a linter.
3. **No dynamic memory after init** — already a global constraint (all buffers static or task-owned; no `malloc`/`free` after startup). Enforced by a linter/grep gate on `malloc`/`calloc`/`realloc`/`free`/`strdup` in in-scope code.
4. **Function length ≤ 60 code lines** — counted as lines of actual code (blank lines, comment-only lines, and brace-only lines excluded). A small number of justified exceptions (e.g. a flat dispatch `switch` clearer whole) may be listed in the deviation register. Baseline: ~11 functions currently exceed this (~6 core, ~4 app, ~1 driver); they are split into named helpers, behavior-preserving.
5. **Assertion density: average ≥ 2 per function** — measured as a codebase (per-component) average, so trivial accessors may carry zero while complex functions carry more. Assertions use the existing `CORE_ASSERT_RET(cond, code, ret)` / `CORE_ASSERT_VOID(cond, code)` (`core/core.h`): on failure they report `code` through `core_set_assert_hook` (the app hook logs to the §17.7 error ring and returns a safe value) and **never abort on the device**. Assertions check genuine anomalies — preconditions, postconditions, invariants, and parameter validity (rule 7) — never tautologies, and are side-effect-free. Baseline: the macros exist but are almost unused, so this is the largest single effort. App/driver code that cannot include `core/core.h` uses an equivalent `LT_ASSERT_*` returning-macro over the same hook (defined once in the app layer).
6. **Smallest-scope data** — declare each object at the innermost scope that suffices; no unnecessary file-scope statics. Enforced by `clang-tidy` (`misc-*`/`readability-*`) plus review.
7. **Check every return value; validate parameters** — every non-`void` call's result is used or explicitly `(void)`-cast with intent; each function validates its parameters (folded into rule 5's assertions). Enforced by `clang-tidy` (`bugprone-unused-return-value`, `cert-err33-c`).
8. **Restricted preprocessor** — includes and simple, parenthesised object-like/function-like macros only; no token pasting, stringisation-as-logic, variadic or recursive macros in shipped code. The `build_config.h` `CFG_*` macros and the `#if CFG_GPS_SIM` conditional compile are permitted (justified: the §4.6 variant mechanism). The `SUITE()` X-macro is test-only (exempt). Enforced by review + a grep gate on `##`/`#x` in in-scope code.
9. **Restricted pointers; no function pointers** — at most one level of dereference in a single expression; no dereference hidden behind a macro/typedef. Function pointers are forbidden except at registered deviation sites, each of which must survive the §2 burden of proof. The candidates split in two:
   - **Genuinely unavoidable — the IDF/FreeRTOS API contract is a function pointer:** the task entry passed to `xTaskCreateStatic*`, the handler passed to `gpio_isr_handler_add` (behind `board_buttons_enable_isr`), and any `esp_timer` callback. There is no compliant alternative short of not using FreeRTOS/IDF; these register with that justification.
   - **The codebase's OWN callbacks — presumed removable until proven otherwise:** `lap`/`drag` `engine_cb` event emit, the `exp` frame callback, the `ses` reader callback, the `sto_list` callback, the `cmd` emit callback, and the `core_set_assert_hook`. During the retrofit each is first attacked with its compliant alternative — e.g. an engine that writes events into a caller-provided output ring/array that the caller drains (no callback); a `ses`/`exp`/`sto` iterator or caller-drained buffer instead of a per-item callback; an assert reporter as a link-time `extern void lt_assert_report(...)` the app defines and `core` calls directly (a normal call, not a stored pointer). It is registered ONLY if that rework is infeasible or clearly not worth its cost. The expectation is that most of these are removed, not registered.
   
   CI blocks any new function pointer absent from the register.
10. **Zero warnings, multiple analysers** — the existing `-Wall -Wextra -Werror -Wshadow -Wconversion` (host + target) plus ASan/UBSan stay, and CI adds `clang-tidy` and `cppcheck` (and the project's custom linter). A clean build under all of them is required.

## 4. The deviation register

`docs/power-of-10-deviations.md`, one row per exception with the §2 burden of proof recorded, not just a justification: `id | rule | file:symbol | compliant alternative considered | why it was rejected (infeasible / cost clearly exceeds benefit) | benefit that justifies the hole | reviewer`. A row without a spelled-out, genuinely-rejected compliant alternative is not a valid deviation. The custom linter reads the register: a function pointer or over-length function is a build failure unless its site is registered. Adding a deviation is a reviewed change held to the same scrutiny as the code. The register is a maintained artifact alongside `docs/measurements.md`, and is expected to be short — only the IDF/FreeRTOS boundary and whatever internal callbacks genuinely could not be reworked.

## 5. Enforcement (CI + review)

- **`clang-tidy`** with a repo `.clang-tidy` enabling the checks that map to rules 1/6/7/9 (`misc-no-recursion`, `bugprone-*`, `cert-*`, `readability-function-size` for rule 4, pointer checks) — run over the in-scope translation units.
- **`cppcheck`** (`--enable=warning,style,portability`, misra-adjacent) as a second analyser (rule 10 "multiple analysers").
- **A small custom linter** (`tools/lint/power_of_10.py`) for the project-specific gates automated tools do not cover well: function length ≤ 60 code lines, per-component assertion-density average ≥ 2, no `malloc`-family after init, no unregistered function pointer, no `##`/token-paste in shipped code. It reads the deviation register.
- **Rollout:** the analysers run **report-only** while the retrofit is in progress (so the build stays green), then flip to **blocking** in the final session once every in-scope file complies. They become required CI checks alongside the existing five.
- Rules not fully automatable (2 bounded-loops provability, 8 preprocessor judgment) are enforced by a **review checklist** carried in the SDD reviewer prompts.

## 6. Behavior-preserving mandate

The retrofit changes *how* code is written, never *what it does*. The existing host test suite, the byte-exact PBM UI goldens, the replay regression fixtures, and the on-device measurements are the regression net: every session keeps them green, and each session that touches firmware flashes the affected env to confirm identical runtime behavior (the standing build-and-flash rule). No behavior change, no interface change, ships in this plan.

## 7. Delivery approach (detailed sessions come from the writing-plans step)

Serial, roughly: (1) standards + tooling + CI report-only + the deviation register + the `LT_ASSERT_*` app macro; (2–4) retrofit by component — `components/core` first (highest value, closest today), then `components/app`, then `components/drivers` + `main`, each an independently reviewed, test-green, behavior-preserving session; (5) flip CI to blocking, wire the rules into the SDD reviewer prompts and the writing-plans checklist, flash to confirm, tag `plan-4.5-done`. Each retrofit session is scoped so a fresh reviewer can gate it.

## 8. Process integration (staying compliant)

From `plan-4.5-done` on: the `subagent-driven-development` task-reviewer and final-review prompts gain a Power-of-10 checklist; the `writing-plans` template notes the rules as a global constraint; new firmware code is born compliant and the blocking CI analysers catch regressions. The product spec §17.9 is amended to point at this design as the authority for the code rules.

## 9. Consequences and risks

- **Cost:** ~5–6 behavior-preserving sessions plus tooling, front-loaded before Plan 5 (accepted). Rule 5 (assertions from ~0 to average ≥2) and rule 4 (split ~11 functions) dominate. Rule 9 is more than cataloguing: under the §2 burden of proof the retrofit actively reworks the codebase's own callbacks (engine event emit → caller-drained output buffer, `exp`/`ses`/`sto` per-item callbacks → iterators/buffers, the assert hook → a link-time `extern` call) rather than registering them, so the internal-callback rework is real engineering, not a catalogue — and it must stay behavior-preserving. The register should end up small (essentially the IDF/FreeRTOS boundary).
- **Main risk:** a behavior-preserving refactor can still introduce a bug. Mitigation: the full existing test/golden/measurement suite gates every session, and firmware sessions flash to confirm. This is the same net that caught the plan-03/04 hardware bugs.
- **Design constraint going forward:** new callbacks require a justified register entry, and functions stay short — a deliberate, mild tax on future design that buys static analysability.
- **Minor:** added assertions grow the image slightly at `-Os`; the deviation register and the checklist become maintained artifacts; Plan 5 (connectivity) is delayed by this plan's duration.
- **Not in scope:** MISRA-C in full, formal verification, or changing the product's behavior/features. This is the ten rules only, on the on-device C.

## 10. Placement

Inserted as **Plan 4.5**, executed after `plan-04-done` and before Plan 5 (Connectivity and OTA), so all remaining feature plans (5–9) are built under the rules. The roadmap gains a Plan 4.5 row; no existing plan is renumbered.
