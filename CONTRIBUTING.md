# Contributing to ProjectCharybdis

Thank you for your interest in contributing to ProjectCharybdis! This is the chaos and
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
- [Pixi](https://pixi.sh/) for environment management
- [Just](https://just.systems/) as the command runner
- C++20 compiler (GCC 12+ or Clang 15+)

### Environment Setup

```bash
# Clone the repository
git clone https://github.com/HomericIntelligence/ProjectCharybdis.git
cd ProjectCharybdis

# Activate the Pixi environment (installs CMake, Ninja, clang-tools, gcovr)
pixi shell

# Build the project
just build

# Run tests to verify setup
just test
```

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

- Browse [existing issues](https://github.com/HomericIntelligence/ProjectCharybdis/issues)
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

### Never Push Directly to Main

The `main` branch is protected. All changes must go through pull requests.

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

#### Prerequisites

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

Thank you for contributing to ProjectCharybdis!
