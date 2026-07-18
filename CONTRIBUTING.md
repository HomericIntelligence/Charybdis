# Contributing to Charybdis

Thank you for your interest in contributing to Charybdis! This is the chaos and
resilience testing framework for the
[HomericIntelligence](https://github.com/HomericIntelligence) distributed agent mesh.

For an overview of the full ecosystem, see the
[Odysseus](https://github.com/HomericIntelligence/Odysseus) meta-repo.

## Quick Links

- [Development Setup](#development-setup)
- [What You Can Contribute](#what-you-can-contribute)
- [Development Workflow](#development-workflow)
- [Building and Testing](#building-and-testing)
- [Pull Request Process](#pull-request-process)
- [Code Review](#code-review)

## Development Setup

### Prerequisites

- [Git](https://git-scm.com/)
- [GitHub CLI](https://cli.github.com/) (`gh`)
- [uv](https://docs.astral.sh/uv/) — manages the build toolchain (CMake, Ninja,
  Conan, gcovr) as locked PyPI wheels (Odysseus ADR-018)
- [Just](https://just.systems/) as the command runner
- C++20 compiler (GCC 12+ or Clang 15+) — installed from the system (apt), e.g.
  `sudo apt-get install build-essential`. Per ADR-018 the compiler is NOT
  provided by uv; only the CMake/Ninja/Conan/gcovr toolchain is.
- [Conan 2.x](https://docs.conan.io/2/) for C++ dependency management — installed
  by `uv sync` (declared in `pyproject.toml`); see [Conan Setup](#conan-setup) below.

### Environment Setup

```bash
# Clone the repository
git clone https://github.com/HomericIntelligence/Charybdis.git
cd Charybdis

# Install the uv-managed build toolchain (CMake, Ninja, Conan, gcovr)
uv sync

# Build the project (justfile recipes invoke the toolchain via `uv run`)
just build

# Run tests to verify setup
just test
```

### Conan Setup

Dependencies (GoogleTest, cpp-httplib, nlohmann_json) are managed by Conan 2.x. The
uv-managed toolchain ships Conan, so `uv sync` is sufficient for most contributors
(recipes call it via `uv run conan …`). To bootstrap Conan manually:

```bash
# 1. Install Conan 2.x (skipped if you use `uv sync`)
pip install 'conan>=2.0'

# 2. Detect a default profile (creates ~/.conan2/profiles/default)
conan profile detect --force

# 3. Install dependencies into the build folder used by the debug preset
conan install . --output-folder=build/debug --profile=conan/profiles/debug --build=missing
```

After Conan finishes, run `cmake --preset debug` followed by `just build` / `just test`.
See [Conan Profiles](#conan-profiles) below for the difference between the `default`
(Release) and `debug` profiles.

### Install Pre-commit Hooks

```bash
# Install hooks (clang-format, conventional commits, trailing whitespace)
pre-commit install
```

### Verify Your Setup

```bash
# List all available recipes
just --list

# Check formatting compliance
just format-check

# Run the full CI pipeline locally
just ci
```

## What You Can Contribute

- **Chaos test scenarios** — New fault injection tests and resilience assertions
- **Fault injection strategies** — Network partitions, latency injection, process crashes
- **Test harnesses** — Fixtures and utilities for chaos testing
- **Tests** — GoogleTest unit tests for chaos components
- **Dockerfile improvements** — Build optimization, security hardening
- **Documentation** — README updates, test scenario descriptions

## Development Workflow

### 1. Find or Create an Issue

Before starting work:

- Browse [existing issues](https://github.com/HomericIntelligence/Charybdis/issues)
- Comment on an issue to claim it before starting work
- Create a new issue if one doesn't exist for your contribution

### 2. Branch Naming Convention

Create a feature branch from `main`:

```bash
git checkout main
git pull origin main
git checkout -b <issue-number>-<short-description>

# Examples:
git checkout -b 5-add-network-partition-test
git checkout -b 3-fix-fault-injection-timeout
```

**Branch naming rules:**

- Start with the issue number
- Use lowercase letters and hyphens
- Keep descriptions short but descriptive

### 3. Commit Message Format

We follow [Conventional Commits](https://www.conventionalcommits.org/):

```text
<type>(<scope>): <subject>

<body>

<footer>
```

**Types:**

| Type       | Description                |
|------------|----------------------------|
| `feat`     | New feature                |
| `fix`      | Bug fix                    |
| `docs`     | Documentation only         |
| `style`    | Formatting, no code change |
| `refactor` | Code restructuring         |
| `test`     | Adding/updating tests      |
| `chore`    | Maintenance tasks          |

**Example:**

```bash
git commit -m "feat(chaos): add network partition fault injection

Implements configurable network partition simulation between
agent groups with automatic recovery after timeout.

Closes #5"
```

## Building and Testing

### Build

```bash
# Debug build (default)
just build

# The build uses CMake with Ninja generator and CMakePresets.json
```

### Test

```bash
# Run all tests via CTest + GoogleTest
just test

# Generate coverage report (gcovr)
just coverage
```

### Running with Sanitizers

The `debug` preset automatically enables AddressSanitizer + UBSan. ThreadSanitizer has its
own preset. Use these to catch memory errors and data races locally before pushing.

```bash
# AddressSanitizer + UBSan (default debug preset)
# Detects: heap/stack buffer overflows, use-after-free, undefined behaviour
just build           # uses --preset debug which sets SANITIZER=asan_ubsan
just test

# ThreadSanitizer — detects data races between threads
# Note: TSan and ASan are mutually exclusive; use separate builds
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

**Suppression files**: If a third-party library triggers a false positive, add an entry to
`sanitizers/asan.supp` or `sanitizers/tsan.supp` (create as needed) and export the path:

```bash
export ASAN_OPTIONS=suppressions=sanitizers/asan.supp
export TSAN_OPTIONS=suppressions=sanitizers/tsan.supp
```

**MemorySanitizer (MSan)** is not available without a fully instrumented libc++ build.
See the MSan section in [SECURITY.md](SECURITY.md) for details.

### Lint and Format

```bash
# Run clang-tidy
just lint

# Check formatting (clang-format v17)
just format-check

# Auto-format all source files
just format
```

### C++ Conventions

- **Standard**: C++20
- **Formatting**: clang-format v17 (enforced by pre-commit hook)
- **Build generator**: Ninja via CMakePresets.json
- **Dependencies**: Managed via Conan 2.x (see `conanfile.py`; includes GoogleTest, cpp-httplib, nlohmann_json)
- **Sanitizers**: Use ASAN/TSAN for debugging memory and threading issues
- **Test isolation**: All fault injection must target test-namespaced NATS subjects only

### Conan Profiles

The project ships two Conan profiles under `conan/profiles/`:

| Profile | `build_type` | When to use |
|---------|-------------|-------------|
| `default` | `Release` | Production/Docker builds. Used by the `Dockerfile` and CI image builds. Optimised binary with no debug symbols. |
| `debug` | `Debug` | Local development. Enables debug symbols; used together with the `debug` CMake preset (ASan + UBSan). |

The `Dockerfile` intentionally uses `--profile=conan/profiles/default` because the image
is a release artifact. Contributors building locally for debugging should pass the debug
profile explicitly:

```bash
conan install . --output-folder=build --profile=conan/profiles/debug --build=missing
cmake --preset debug
```

Do not use the debug profile inside Docker — it produces larger binaries and is not
suitable for the production runtime image.

## Pull Request Process

### Before You Start

1. Ensure an issue exists for your work
2. Create a branch from `main` using the naming convention
3. Implement your changes
4. Run `just ci` locally to verify build, test, lint, and format checks pass

### Creating Your Pull Request

```bash
git push -u origin <branch-name>
gh pr create --title "[Type] Brief description" --body "Closes #<issue-number>"
```

**PR Requirements:**

- PR must be linked to a GitHub issue
- PR title should be clear and descriptive
- All CI checks must pass (build, test, lint, format)

### Merge queue rollout

Required checks are prepared for merge groups, but queue activation is a
separate Odysseus-controlled operator action. See
[`docs/ci/merge-queue.md`](docs/ci/merge-queue.md) for the policy contract,
review gate, activation boundary, and post-merge smoke requirement.

### Never Push Directly to Main

The `main` branch is protected. All changes must go through pull requests.

### CI Gates

PRs must pass these required checks before merge:

- **Build + Test** — `build-test.yml` (GCC and Clang).
- **Static analysis** — clang-tidy via `static-analysis.yml`.
- **Sanitizers** — ASan/UBSan via `sanitizers.yml`.
- **CodeQL (SAST)** — `.github/workflows/codeql.yml` runs on every PR. Findings block
  merge until resolved or triaged. To reproduce locally, install the
  [CodeQL CLI](https://docs.github.com/en/code-security/codeql-cli) and run:

  ```bash
  codeql database create --language=cpp --command "cmake --build --preset debug" db
  codeql analyze db --format=sarif-latest --output=results.sarif
  ```

  False positives should be triaged by the security maintainers; open an issue with
  the SARIF excerpt rather than disabling the rule unilaterally.
- **Secrets scan** — gitleaks via `_required.yml` (`security-secrets-scan` job).
- **Container build + Trivy scan** — `container.yml` and the `release.yml` Trivy gate.

## Release Process

Releases are cut from `main` by tagging a semver `vX.Y.Z` tag. The release workflow
(`.github/workflows/release.yml`) is triggered by `v*.*.*` tag pushes.

```bash
# After the release PR (CHANGELOG.md update) is merged to main
git checkout main && git pull origin main
git tag -a v0.1.0 -m "Release v0.1.0"
git push origin v0.1.0
```

The first release (`v0.1.0`) is the bootstrap step that proves the workflow end-to-end.
Subsequent releases follow the same procedure with the next semver tag.

## Roadmap Maintenance

`ROADMAP.md` mirrors the GitHub milestones. To keep them in sync:

1. **When closing a milestone issue**, check off the matching item in `ROADMAP.md`
   in the same PR (or the PR that closes the issue). Do not leave roadmap updates
   for a separate cleanup pass.
2. **When creating a new milestone**, add a corresponding section to `ROADMAP.md`
   in the same change set (or a follow-up PR opened the same day).
3. **When renaming or retargeting a milestone**, update `ROADMAP.md` to match.

Drift between `ROADMAP.md` and the milestone list is treated as a documentation bug.

## Code Review

### What Reviewers Look For

- **Test isolation** — Do chaos tests stay within test namespaces?
- **Correctness** — Does the fault injection behave as documented?
- **Memory safety** — No buffer overflows, dangling pointers, or data races
- **Cleanup** — Do tests clean up injected faults on completion or failure?
- **Formatting** — Does `just format-check` pass?

### Responding to Review Comments

- Keep responses short (1 line preferred)
- Start with "Fixed -" to indicate resolution

## Markdown Standards

All documentation files must follow these standards:

- Code blocks must have a language tag (`cpp`, `bash`, `yaml`, `text`, etc.)
- Code blocks must be surrounded by blank lines
- Lists must be surrounded by blank lines
- Headings must be surrounded by blank lines

## Reporting Issues

### Bug Reports

Include: clear title, steps to reproduce, expected vs actual behavior, compiler/OS details.

### Security Issues

**Do not open public issues for security vulnerabilities.**
See [SECURITY.md](SECURITY.md) for the responsible disclosure process.

## Maintainer Tooling

### apply-branch-protection.sh — Required Token Scopes

`scripts/apply-branch-protection.sh` configures the `homeric-main-baseline` ruleset via
the GitHub Rulesets API. It requires elevated permissions that are **not** granted by a
standard developer token.

#### Token Prerequisites

| Requirement | Detail |
|-------------|--------|
| `repo` scope | Full repository access (read + write) |
| Org admin rights | Required to modify rulesets (`admin:org` **or** repo admin role) |

#### Obtaining a Suitable Token

1. Go to **GitHub → Settings → Developer settings → Personal access tokens → Tokens (classic)**.
2. Click **Generate new token (classic)**.
3. Select the `repo` scope (full control of private repositories).
4. If you are an org owner, the token inherits admin rights automatically.
   If not, ask an org owner to run the script or grant you the **repository admin** role.
5. Copy the token and configure `gh`:

```bash
gh auth login --with-token <<< "ghp_YOUR_TOKEN_HERE"
# Verify:
gh auth status
```

#### Running the Script

```bash
bash scripts/apply-branch-protection.sh
```

A `403 Forbidden` response from the GitHub API means either the `repo` scope is missing
or the authenticated user does not have org admin rights. Re-check the scopes with
`gh auth status` and regenerate the token if needed.

## Code of Conduct

Please review our [Code of Conduct](CODE_OF_CONDUCT.md) before contributing.

---

Thank you for contributing to Charybdis!
