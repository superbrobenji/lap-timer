# Plan 00: Development Environment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the repository on GitHub with a branch model, CI workflows that stay green from the first push, repo hygiene files, the host and ESP-IDF toolchains, and a first contact with the dev board that settles the flash-size question.

**Architecture:** Trunk-based development on `main` with one short-lived branch per roadmap session, merged by pull request with CI required. Two GitHub Actions workflows: `ci.yml` (host tests, track-table check) and `firmware.yml` (ESP-IDF container build matrix). Both are guarded so they pass before the code they build exists.

**Tech Stack:** git, GitHub CLI (`gh`, already authenticated as `superbrobenji`), GitHub Actions, Homebrew (cmake present; ninja to add), Python 3 venv, ESP-IDF v5.3.x, esptool.

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §21 (build system), §22.5–22.6 (CI), §25 item 1 (flash size).

**Roadmap:** `docs/superpowers/plans/2026-09-14-roadmap.md` sessions 0.1 and 0.2.

## Global Constraints

- Default branch is `main`. Session branches are named `s<plan>.<day>-<topic>` (e.g. `s1.1-harness`). Merge by pull request, squash merge, delete branch. Tags (`pNN-dD`, `plan-NN-done`, `vX.Y.Z`) are created on `main` after merge.
- CI must be green on every push to `main`. Workflows probe for their inputs after checkout and run a green no-op step when the inputs do not exist yet.
- No secrets in the repository. The OTA signing key (plan 5.5) will live in a GitHub Actions secret `OTA_SIGNING_KEY`; `*.pem` stays git-ignored except the committed public key.
- Working directory for all commands: `/Users/benji/projects/personal/lap-timer`.

---

## Session 0.1 — GitHub, branches, workflows

### Task 1: Repository hygiene files

**Files:**
- Create: `README.md`, `.editorconfig`, `.clang-format`, `.gitattributes`, `.github/PULL_REQUEST_TEMPLATE.md`

- [ ] **Step 1: Write `README.md`**

```markdown
# lap-timer

ESP32 GPS + IMU lap timer for track days and drag runs. Battery powered, fully offline, exports to RaceChrono over BLE.

- Design spec (source of truth): `docs/superpowers/specs/2026-09-14-lap-timer-design.md`
- Roadmap and session plans: `docs/superpowers/plans/`

## Layout

| Path | What |
|------|------|
| `components/core/` | pure C11 algorithm core, no ESP-IDF, host-tested |
| `components/hal/` | hardware interface headers |
| `components/drivers/` | one driver per HAL interface, selected at build time |
| `components/app/` | ESP-IDF tasks and glue |
| `test/` | host unit tests (CMake + Unity) |
| `tools/` | web client, replay, generators, scripts |

## Host tests

    cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure

## Firmware

    ./build.sh moto_neo6m build        # see build.sh for environments
    ./build.sh moto_neo6m flash monitor --port /dev/cu.usbserial-XXXX

Disconnect the battery pack before connecting USB (spec §3.2).

## Branching

`main` is always releasable. One branch per roadmap session, named `s<plan>.<day>-<topic>`, merged by squash PR once CI is green. Tags `pNN-dD` mark finished sessions, `plan-NN-done` finished plans, `vX.Y.Z` releases.
```

- [ ] **Step 2: Write `.editorconfig`**

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true
indent_style = space
indent_size = 4

[*.{yml,yaml,json,md}]
indent_size = 2

[*.py]
indent_size = 4

[Makefile]
indent_style = tab
```

- [ ] **Step 3: Write `.clang-format`**

```yaml
BasedOnStyle: LLVM
Language: Cpp
IndentWidth: 4
ColumnLimit: 140
BreakBeforeBraces: Linux
AllowShortFunctionsOnASingleLine: All
AllowShortIfStatementsOnASingleLine: WithoutElse
AlignConsecutiveAssignments: false
PointerAlignment: Right
SortIncludes: false
```

- [ ] **Step 4: Write `.gitattributes`**

```
* text=auto eol=lf
*.pbm binary
*.bin binary
*.ubx binary
*.png binary
```

- [ ] **Step 5: Write `.github/PULL_REQUEST_TEMPLATE.md`**

```markdown
## Session

Roadmap session: <!-- e.g. 1.2 -->

## Checklist

- [ ] All tasks of the session plan checked off
- [ ] `ctest` green locally (host) / bench check done (firmware)
- [ ] Spec updated for any behaviour change discovered during implementation
- [ ] No secrets, no generated binaries
```

- [ ] **Step 6: Commit**

```bash
git add README.md .editorconfig .clang-format .gitattributes .github/PULL_REQUEST_TEMPLATE.md
git commit -m "chore: repository hygiene files and README"
```

### Task 2: Guarded CI workflows

**Files:**
- Create: `.github/workflows/ci.yml`, `.github/workflows/firmware.yml`

- [ ] **Step 1: Write `ci.yml`**

```yaml
name: ci
on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

jobs:
  hygiene:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: No trailing whitespace or CRLF in tracked text files
        run: git diff --check $(git hash-object -t tree /dev/null) HEAD -- . ':!*.pbm' ':!*.bin'
      - name: No committed secrets or build output
        run: |
          ! git ls-files | grep -E '(^|/)(build|test/build)/|\.pem$' | grep -v 'laptimer_pub\.pem$'

  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Probe inputs
        id: probe
        run: echo "present=$([ -f test/CMakeLists.txt ] && echo true || echo false)" >> "$GITHUB_OUTPUT"
      - name: Configure
        if: steps.probe.outputs.present == 'true'
        run: cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        if: steps.probe.outputs.present == 'true'
        run: cmake --build test/build --parallel
      - name: Test
        if: steps.probe.outputs.present == 'true'
        run: ctest --test-dir test/build --output-on-failure
      - name: Nothing to do
        if: steps.probe.outputs.present != 'true'
        run: echo "test/CMakeLists.txt not present yet"

  tracks-generated:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Probe inputs
        id: probe
        run: echo "present=$([ -f tools/tracks/gen_tracks.py ] && echo true || echo false)" >> "$GITHUB_OUTPUT"
      - name: Regenerate bundled tracks and check they are committed
        if: steps.probe.outputs.present == 'true'
        run: |
          python3 tools/tracks/gen_tracks.py tools/tracks/*.json -o components/core/tracks/trk_bundled.c
          git diff --exit-code components/core/tracks/trk_bundled.c
      - name: Nothing to do
        if: steps.probe.outputs.present != 'true'
        run: echo "tools/tracks/gen_tracks.py not present yet"
```

- [ ] **Step 2: Write `firmware.yml`**

```yaml
name: firmware
on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        env: [moto_neo6m, moto_sim]
    container:
      image: espressif/idf:v5.3.2
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Probe inputs
        id: probe
        run: echo "present=$([ -f build.sh ] && echo true || echo false)" >> "$GITHUB_OUTPUT"
      - name: Build ${{ matrix.env }}
        if: steps.probe.outputs.present == 'true'
        shell: bash
        run: |
          . $IDF_PATH/export.sh
          ./build.sh ${{ matrix.env }} build
          ./build.sh ${{ matrix.env }} size
      - uses: actions/upload-artifact@v4
        if: steps.probe.outputs.present == 'true'
        with:
          name: laptimer-${{ matrix.env }}
          path: build/${{ matrix.env }}/*.bin
          if-no-files-found: ignore
      - name: Nothing to do
        if: steps.probe.outputs.present != 'true'
        run: echo "build.sh not present yet"
```

The `espressif/idf` tag must match `.idf-version` (created in Task 6). `moto_sim` is the `GPS=sim IMU=sim` environment used until sensors arrive (spec §4.6).

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/firmware.yml
git commit -m "ci: guarded host-test and firmware workflows"
```

### Task 3: Create the GitHub repository and push

- [ ] **Step 1: Confirm auth and branch**

Run: `gh auth status && git branch --show-current`
Expected: logged in as `superbrobenji`; branch `main`.

- [ ] **Step 2: Create the repository from the local tree**

```bash
gh repo create lap-timer --private --source=. --remote=origin --push --description "ESP32 GPS/IMU lap timer, battery powered, offline, RaceChrono export"
```
Expected: `✓ Created repository superbrobenji/lap-timer on GitHub` and `main` pushed.

(Private repositories on a free GitHub plan cannot use branch protection rules. If protection is wanted, create the repository with `--public` instead, or accept the protocol without enforcement.)

- [ ] **Step 3: Watch the first workflow run**

Run: `gh run list --limit 5` then `gh run watch` on the `ci` run.
Expected: `hygiene` succeeds; `host-tests`, `tracks-generated`, and `firmware` run and finish on their 'Nothing to do' step (inputs absent).

- [ ] **Step 4: Repository settings**

```bash
gh repo edit superbrobenji/lap-timer --delete-branch-on-merge --enable-squash-merge --enable-merge-commit=false --enable-rebase-merge=false --default-branch main
```

If the repository is public (or the plan allows it), protect `main`:
```bash
gh api -X PUT repos/superbrobenji/lap-timer/branches/main/protection \
  -F required_status_checks[strict]=true \
  -F 'required_status_checks[contexts][]=hygiene' \
  -F enforce_admins=false \
  -F required_pull_request_reviews=null \
  -F restrictions=null \
  -F allow_force_pushes=false \
  -F allow_deletions=false
```
Expected: HTTP 200 with the protection object. On a private free-plan repository this returns 403 "Upgrade to GitHub Pro"; record that in the README Branching section and continue.

### Task 4: Session branch workflow dry run

- [ ] **Step 1: Create the first session branch and a trivial change**

```bash
git switch -c s0.1-dev-env
printf '\n## Status\n\nSession 0.1 complete.\n' >> README.md
git commit -am "docs: mark session 0.1"
git push -u origin s0.1-dev-env
```

- [ ] **Step 2: Open and merge the pull request**

```bash
gh pr create --fill --base main
gh pr checks --watch
gh pr merge --squash --delete-branch
git switch main && git pull --ff-only
```
Expected: PR checks show `hygiene` passed and the probed jobs finish on 'Nothing to do'; squash merge lands on `main`.

- [ ] **Step 3: Tag the session**

```bash
git tag p00-d1 && git push origin p00-d1
```

---

## Session 0.2 — Toolchains and first board contact

### Task 5: Host toolchain

- [ ] **Step 1: Install ninja and confirm cmake**

Run: `brew install ninja && cmake --version && ninja --version`
Expected: cmake ≥ 3.16, ninja prints a version.

- [ ] **Step 2: Python virtual environment for tools**

Create `tools/requirements.txt`:
```
pyserial==3.5
Pillow==10.4.0
esptool==4.8.1
```
Run:
```bash
python3 -m venv tools/.venv
tools/.venv/bin/pip install -r tools/requirements.txt
echo 'tools/.venv/' >> .gitignore
tools/.venv/bin/esptool.py version
```
Expected: `esptool.py v4.8.1`.

- [ ] **Step 3: Commit**

```bash
git add tools/requirements.txt .gitignore
git commit -m "chore: python tools environment"
```

### Task 6: ESP-IDF pinned install

- [ ] **Step 1: Pin the version**

Create `.idf-version` containing exactly:
```
v5.3.2
```

- [ ] **Step 2: Install ESP-IDF**

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.2 --recursive --depth 1 https://github.com/espressif/esp-idf.git esp-idf-v5.3.2
cd esp-idf-v5.3.2 && ./install.sh esp32
```
Expected: ends with `All done! You can now run: . ./export.sh`.

- [ ] **Step 3: Add an activation helper**

Create `tools/idf-env.sh`:
```bash
#!/usr/bin/env bash
# usage: source tools/idf-env.sh
IDF_VER="$(cat "$(dirname "${BASH_SOURCE[0]}")/../.idf-version")"
export IDF_PATH="$HOME/esp/esp-idf-$IDF_VER"
[ -d "$IDF_PATH" ] || { echo "ESP-IDF $IDF_VER not found at $IDF_PATH"; return 1; }
. "$IDF_PATH/export.sh" > /dev/null
echo "ESP-IDF $(idf.py --version)"
```

Run: `source tools/idf-env.sh`
Expected: `ESP-IDF ESP-IDF v5.3.2`.

- [ ] **Step 4: Commit**

```bash
chmod +x tools/idf-env.sh
git add .idf-version tools/idf-env.sh
git commit -m "chore: pin ESP-IDF v5.3.2 and add environment helper"
```

### Task 7: First board contact and flash-size verification

- [ ] **Step 1: Connect the dev board over USB (battery not connected) and find the port**

Run: `ls /dev/cu.*`
Expected: a `/dev/cu.usbserial-XXXX` (CH340) entry appears. If not, install the WCH CH34x driver for macOS and re-plug.

- [ ] **Step 2: Read chip and flash information**

Run: `tools/.venv/bin/esptool.py --port /dev/cu.usbserial-XXXX flash_id`
Expected output includes `Chip is ESP32-D0WD…`, `Detected flash size: 4MB` (or larger).

- [ ] **Step 3: Record the result in the spec**

Edit `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §3.1 MCU row: replace `Assumed 4 MB flash **[VERIFY]** \`esptool.py flash_id\`.` with `Flash <size> MB, verified 2026-<mm>-<dd> with esptool flash_id (chip <revision>).` and in §25 mark item 1 as done. If the flash is larger than 4 MB, add a note to §19.1 that `storage` grows accordingly (the CSV is regenerated by `build.sh --flash-size`).

- [ ] **Step 4: Blink-free smoke test with the IDF hello-world**

```bash
source tools/idf-env.sh
cp -r $IDF_PATH/examples/get-started/hello_world /tmp/hello && cd /tmp/hello
idf.py set-target esp32 && idf.py build && idf.py -p /dev/cu.usbserial-XXXX flash monitor
```
Expected: `Hello world!` and chip info on the monitor; `Ctrl+]` exits. This proves toolchain, USB, and CH340 before plan 3.

- [ ] **Step 5: Commit, PR, tag**

```bash
cd /Users/benji/projects/personal/lap-timer
git switch -c s0.2-toolchain   # if not already on a session branch
git add docs/superpowers/specs/2026-09-14-lap-timer-design.md
git commit -m "docs: record verified flash size"
git push -u origin s0.2-toolchain && gh pr create --fill --base main && gh pr checks --watch && gh pr merge --squash --delete-branch
git switch main && git pull --ff-only && git tag p00-d2 && git push origin p00-d2
```

---

## Self-review

- Spec §21.5 versioning (`git describe --tags`) needs at least one tag on `main`: `p00-d1` provides it.
- Spec §21.6 CI: both workflows exist from session 0.1; plan 1 task 13 now only removes nothing and verifies the `host-tests` job runs once `test/CMakeLists.txt` exists (updated in plan 01).
- Spec §25 item 1 (flash size) resolved in Task 7.
- No placeholders; every command is concrete. The only user-specific value is the serial port name, which is discovered in Task 7 step 1.
