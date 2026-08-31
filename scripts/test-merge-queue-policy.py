#!/usr/bin/env python3
"""Behavioral regression tests for merge-queue / PR parity."""

from __future__ import annotations

import json
import re
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS_DIR = REPO_ROOT / ".github" / "workflows"
POLICY_PATH = REPO_ROOT / "configs" / "github" / "merge-queue-policy.json"
DOC_PATH = REPO_ROOT / "docs" / "ci" / "merge-queue.md"
JUSTFILE_PATH = REPO_ROOT / "justfile"
PRECOMMIT_PATH = REPO_ROOT / ".pre-commit-config.yaml"

EXPECTED_CONTEXTS = [
    "All Build/Test Checks",
    "All Coverage Checks",
    "All Static Analysis Checks",
    "build",
    "deps/version-sync",
    "install",
    "integration-tests",
    "lint",
    "package",
    "release",
    "schema-validation",
    "security/dependency-scan",
    "security/secrets-scan",
    "test",
    "typecheck",
    "unit-tests",
]

EXPECTED_QUEUE_RULE = {
    "type": "merge_queue",
    "parameters": {
        "check_response_timeout_minutes": 60,
        "grouping_strategy": "ALLGREEN",
        "max_entries_to_build": 10,
        "max_entries_to_merge": 5,
        "merge_method": "SQUASH",
        "min_entries_to_merge": 1,
        "min_entries_to_merge_wait_minutes": 5,
    },
}

EXPECTED_REQUIRED_WORKFLOWS = {
    "_required.yml",
    "build-test.yml",
    "code-coverage.yml",
    "container.yml",
    "integration-tests.yml",
    "static-analysis.yml",
}

# Parity design: PR and merge_group run the same workflows.
# Every workflow that handles pull_request (except release/ruleset-audit)
# must also handle merge_group so required contexts are validated on the
# merge commit itself, not just the PR head.
EXPECTED_MERGE_GROUP_WORKFLOWS = {
    "_required.yml",
    "build-test.yml",
    "code-coverage.yml",
    "codeql.yml",
    "container.yml",
    "integration-tests.yml",
    "lock-check.yml",
    "sanitizers.yml",
    "static-analysis.yml",
}

EXPECTED_FAN_IN_EMITTERS = {
    "build": ("build-test.yml", "build"),
    "unit-tests": ("build-test.yml", "unit-tests"),
    "lint": ("static-analysis.yml", "lint"),
    "package": ("container.yml", "package"),
}

EXPECTED_TEST_EMITTERS = [
    "build-test.yml:test",
]

EXPECTED_REQUIRED_JOB_IDS = {
    "deps-version-sync",
    "forbid-suppressions",
    "install",
    "lockfile-integrity",
    "release",
    "schema-validation",
    "security-dependency-scan",
    "security-secrets-scan",
    "typecheck",
    "uv-lock-check",
}

EXPECTED_REQUIRED_VALIDATORS: dict[str, tuple[str, tuple[str, ...]]] = {
    "deps-version-sync": (
        "deps-version-sync (in container)",
        ("bash scripts/run_ci_local.sh deps-version-sync",),
    ),
    "forbid-suppressions": (
        "forbid-suppressions (in container)",
        ("bash scripts/run_ci_local.sh forbid-suppressions",),
    ),
    "install": (
        "Install to staging prefix",
        (
            'uv run cmake --install build/release --prefix '
            '"${RUNNER_TEMP}/install-prefix"',
        ),
    ),
    "lockfile-integrity": (
        "Regenerate lockfile",
        (
            "conan lock create . -s compiler=gcc -s compiler.version=14 "
            "-s compiler.libcxx=libstdc++11 -s compiler.cppstd=20 "
            "-s build_type=Debug -s os=Linux -s arch=x86_64",
        ),
    ),
    "release": (
        "Dry-run changelog generation",
        ('FRAGMENT=$(bash scripts/generate-changelog.sh "${PREV_TAG}" "HEAD")',),
    ),
    "schema-validation": (
        "schema-validation (in container)",
        ("bash scripts/run_ci_local.sh schema-validation",),
    ),
    "security-dependency-scan": (
        "security-dependency-scan (in container)",
        ("bash scripts/run_ci_local.sh security-dependency-scan",),
    ),
    "security-secrets-scan": (
        "Run Gitleaks",
        (
            "podman run --rm --userns=keep-id:uid=1000,gid=1000 "
            '-v "$PWD:/workspace:Z" -w /workspace charybdis-ci:local '
            "gitleaks detect --source /workspace --config .gitleaks.toml "
            "--report-format sarif --report-path gitleaks.sarif --redact",
            "podman run --rm --userns=keep-id:uid=1000,gid=1000 "
            '-v "$PWD:/workspace:Z" -w /workspace charybdis-ci:local '
            "gitleaks detect --source /workspace "
            "--report-format sarif --report-path gitleaks.sarif --redact",
        ),
    ),
    "typecheck": (
        "typecheck (in container)",
        ("bash scripts/run_ci_local.sh typecheck",),
    ),
    "uv-lock-check": (
        "uv-lock-check (in container)",
        ("bash scripts/run_ci_local.sh uv-lock-check",),
    ),
}

ALLOWED_REQUIRED_STEP_CONDITIONS = {
    (
        "security-secrets-scan",
        "Upload Gitleaks SARIF",
    ): "always() && hashFiles('gitleaks.sarif') != '' "
    "&& github.event_name != 'merge_group'",
}

EXPECTED_REQUIRED_TRIGGERS = {
    "pull_request": {"branches": ["main"]},
    "push": {"branches": ["main"]},
    "merge_group": {"types": ["checks_requested"]},
}


def _inline_list(value: str) -> list[str]:
    """Parse the simple inline YAML lists used by workflow trigger blocks."""
    match = re.fullmatch(r"\[(.*)]", value.strip())
    if not match:
        raise AssertionError(f"expected an inline YAML list, got {value!r}")
    body = match.group(1).strip()
    if not body:
        return []
    return [item.strip().strip("'\"") for item in body.split(",")]


def _triggers_from_text(text: str) -> dict[str, dict[str, list[str]]]:
    """Read top-level Actions events and their inline list settings."""
    lines = text.splitlines()
    start = lines.index("on:") + 1
    events: dict[str, dict[str, list[str]]] = {}
    event: str | None = None

    for line in lines[start:]:
        if line and not line.startswith((" ", "#")):
            break
        event_match = re.match(r"^  ([a-z_]+):(?:\s*(.*))?$", line)
        if event_match:
            event = event_match.group(1)
            events[event] = {}
            continue
        setting_match = re.match(r"^    (branches|tags|types):\s*(\[.*])$", line)
        if setting_match and event is not None:
            events[event][setting_match.group(1)] = _inline_list(
                setting_match.group(2)
            )

    return events


def _triggers(path: Path) -> dict[str, dict[str, list[str]]]:
    """Read top-level Actions events and their inline list settings."""
    return _triggers_from_text(path.read_text())


def _job_contexts_from_text(text: str) -> list[tuple[str, str]]:
    """Return each job id and the check context it emits."""
    lines = text.splitlines()
    start = lines.index("jobs:") + 1
    contexts: list[tuple[str, str]] = []
    job_id: str | None = None
    job_name: str | None = None

    for line in lines[start:]:
        if line and not line.startswith((" ", "#")):
            break
        job_match = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if job_match:
            if job_id is not None:
                contexts.append((job_id, job_name or job_id))
            job_id = job_match.group(1)
            job_name = None
            continue
        name_match = re.match(r"^    name:\s*(.+?)\s*$", line)
        if job_id is not None and name_match:
            job_name = name_match.group(1).strip("'\"")

    if job_id is not None:
        contexts.append((job_id, job_name or job_id))

    return contexts


def _job_contexts(path: Path) -> list[tuple[str, str]]:
    """Return each job id and the check context it emits."""
    return _job_contexts_from_text(path.read_text())


def _job_section_from_text(text: str, job_id: str) -> str:
    """Return one job definition from a workflow's jobs section."""
    lines = text.splitlines()
    start = lines.index("jobs:") + 1
    job_line = f"  {job_id}:"
    job_start = next(index for index in range(start, len(lines)) if lines[index] == job_line)
    job_end = len(lines)
    for index in range(job_start + 1, len(lines)):
        if re.match(r"^  [A-Za-z0-9_-]+:\s*$", lines[index]):
            job_end = index
            break
    return "\n".join(lines[job_start:job_end])


def _job_section(path: Path, job_id: str) -> str:
    """Return one job definition from a workflow's jobs section."""
    return _job_section_from_text(path.read_text(), job_id)


def _top_level_block(text: str, key: str) -> str:
    """Return one top-level YAML block from a workflow."""
    lines = text.splitlines()
    start = lines.index(f"{key}:")
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if lines[index] and not lines[index].startswith((" ", "#")):
            end = index
            break
    return "\n".join(lines[start:end])


def _job_step_sections(section: str) -> list[str]:
    """Return each step definition from one job section."""
    lines = section.splitlines()
    starts = [
        index for index, line in enumerate(lines) if re.match(r"^      - ", line)
    ]
    return [
        "\n".join(lines[start : starts[position + 1] if position + 1 < len(starts) else len(lines)])
        for position, start in enumerate(starts)
    ]


def _step_field(section: str, field: str) -> tuple[int, str] | None:
    """Return a step field's line index and scalar value."""
    patterns = (
        rf"^      - {re.escape(field)}:\s*(.*?)\s*$",
        rf"^        {re.escape(field)}:\s*(.*?)\s*$",
    )
    for index, line in enumerate(section.splitlines()):
        pattern = patterns[0] if index == 0 else patterns[1]
        match = re.match(pattern, line)
        if match:
            return index, match.group(1)
    return None


def _step_name(section: str) -> str | None:
    """Return a step's explicit display name."""
    field = _step_field(section, "name")
    return field[1].strip("'\"") if field else None


def _step_identity(section: str) -> str:
    """Return a stable identity for diagnostics and condition allowlisting."""
    name = _step_name(section)
    if name is not None:
        return name
    return section.splitlines()[0].removeprefix("      - ").strip()


def _step_condition(section: str) -> str | None:
    """Return a step-level condition, if one exists."""
    field = _step_field(section, "if")
    return field[1] if field else None


def _step_run_command(section: str) -> str | None:
    """Return one step's shell command without its YAML indentation."""
    lines = section.splitlines()
    field = _step_field(section, "run")
    if field is None:
        return None
    index, value = field
    if value not in {"|", "|-", ">", ">-"}:
        return value

    block: list[str] = []
    for block_line in lines[index + 1 :]:
        if block_line and not block_line.startswith("          "):
            break
        block.append(block_line[10:] if block_line else "")
    return "\n".join(block)



def _logical_shell_commands(run_command: str) -> list[str]:
    """Join shell continuation lines into ordered executable commands."""
    commands: list[str] = []
    fragments: list[str] = []
    for raw_line in run_command.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        continued = line.endswith("\\")
        fragments.append(line.removesuffix("\\").rstrip())
        if not continued:
            commands.append(" ".join(fragments))
            fragments = []

    return commands


def _has_all_executable_shell_commands(
    run_command: str | None, expected_commands: tuple[str, ...]
) -> bool:
    """Return whether a run step executes every exact logical shell command."""
    if run_command is None:
        return False

    return set(expected_commands).issubset(_logical_shell_commands(run_command))


def _has_bound_gitleaks_branches(
    run_command: str | None, expected_commands: tuple[str, ...]
) -> bool:
    """Bind each Gitleaks invocation to its reachable configuration branch."""
    if run_command is None or len(expected_commands) != 2:
        return False

    configured_command, default_command = expected_commands
    return _logical_shell_commands(run_command) == [
        "if [ -f .gitleaks.toml ]; then",
        configured_command,
        "else",
        default_command,
        "fi",
    ]


def _workflow_display_name(path: Path) -> str | None:
    """Return the top-level workflow display name."""
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"name:\s*(.+?)\s*", line)
        if match:
            return match.group(1).strip("'\"")
    return None


def _required_graph_violations(
    text: str, required_check_workflows: list[str]
) -> list[str]:
    """Return violations of the one-run Required Checks contract."""
    violations: list[str] = []
    triggers = _triggers_from_text(text)
    if triggers != EXPECTED_REQUIRED_TRIGGERS:
        violations.append(f"direct trigger set differs: {triggers!r}")
    if "workflow_run" in text:
        violations.append("workflow_run creates a duplicate execution path")

    concurrency = _top_level_block(text, "concurrency")
    for identity in ("${{ github.event_name }}", "${{ github.sha }}"):
        if identity not in concurrency:
            violations.append(f"concurrency omits {identity}")
    if "cancel-in-progress: false" not in concurrency:
        violations.append("concurrency can cancel an authoritative run")

    job_ids = {job_id for job_id, _ in _job_contexts_from_text(text)}
    missing = EXPECTED_REQUIRED_JOB_IDS - job_ids
    unexpected = job_ids - EXPECTED_REQUIRED_JOB_IDS
    if missing:
        violations.append(f"missing validators: {sorted(missing)!r}")
    if unexpected:
        violations.append(f"hollow or duplicate jobs: {sorted(unexpected)!r}")

    validator_job_ids = set(EXPECTED_REQUIRED_VALIDATORS)
    if validator_job_ids != EXPECTED_REQUIRED_JOB_IDS:
        violations.append("validator-step contract does not match expected jobs")

    for job_id in sorted(EXPECTED_REQUIRED_JOB_IDS & job_ids & validator_job_ids):
        section = _job_section_from_text(text, job_id)
        if re.search(r"(?m)^    needs:", section):
            violations.append(f"independent validator declares needs: {job_id}")
        if re.search(r"(?m)^    if:", section):
            violations.append(f"conditional job can mask a result: {job_id}")
        if re.search(r"(?m)^    continue-on-error:", section):
            violations.append(f"job continue-on-error forbidden: {job_id}")
        if not re.search(r"(?m)^      - uses: actions/checkout@", section):
            violations.append(f"validator does not check out source: {job_id}")

        validator_name, expected_commands = EXPECTED_REQUIRED_VALIDATORS[job_id]
        steps = _job_step_sections(section)
        for step in steps:
            step_name = _step_identity(step)
            if _step_field(step, "continue-on-error") is not None:
                violations.append(
                    f"step continue-on-error forbidden: {job_id} / {step_name}"
                )
            condition = _step_condition(step)
            if condition is None:
                continue
            allowed = ALLOWED_REQUIRED_STEP_CONDITIONS.get((job_id, step_name))
            if condition != allowed:
                kind = (
                    "validator step condition not allowlisted"
                    if step_name == validator_name
                    else "step condition not allowlisted"
                )
                violations.append(f"{kind}: {job_id} / {step_name}")

        validator_steps = [
            step for step in steps if _step_name(step) == validator_name
        ]
        run_command = (
            _step_run_command(validator_steps[0])
            if len(validator_steps) == 1
            else None
        )
        commands_valid = _has_all_executable_shell_commands(
            run_command, expected_commands
        )
        if job_id == "security-secrets-scan":
            commands_valid = commands_valid and _has_bound_gitleaks_branches(
                run_command, expected_commands
            )
        if len(validator_steps) != 1 or not commands_valid:
            violations.append(f"validator command missing: {job_id}")

    if required_check_workflows.count("Required Checks") != 1:
        violations.append("Required Checks has a duplicate workflow entry point")
    return violations


def _remove_job(text: str, job_id: str) -> str:
    """Remove one job from a workflow fixture."""
    section = _job_section_from_text(text, job_id)
    return text.replace(f"{section}\n", "", 1)


def _add_job_setting(text: str, job_id: str, setting: str) -> str:
    """Add one job-level setting to a workflow fixture."""
    marker = f"  {job_id}:\n"
    return text.replace(marker, f"{marker}{setting}", 1)


def _replace_job(text: str, job_id: str, replacement: str) -> str:
    """Replace one job in a workflow fixture."""
    section = _job_section_from_text(text, job_id)
    return text.replace(section, replacement, 1)


def _job_names(path: Path) -> list[str]:
    """Return job check contexts, including ids without an explicit name."""
    return [name for _, name in _job_contexts(path)]


def _workflow_paths(directory: Path = WORKFLOWS_DIR) -> list[Path]:
    """Return all GitHub workflow files for both supported YAML suffixes."""
    return sorted((*directory.glob("*.yml"), *directory.glob("*.yaml")))


def _context_emitters(context: str) -> list[str]:
    """Return workflow/job emitters for a required check context."""
    emitters: list[str] = []
    for workflow in _workflow_paths():
        for job_id, job_name in _job_contexts(workflow):
            if job_name == context:
                emitters.append(f"{workflow.name}:{job_id}")
    return emitters


def _policy() -> dict[str, object]:
    return json.loads(POLICY_PATH.read_text())


class MergeQueuePolicyTests(unittest.TestCase):
    def test_policy_pins_live_required_contexts_and_queue_rule(self) -> None:
        policy = _policy()

        self.assertEqual(policy["repository"], "HomericIntelligence/Charybdis")
        self.assertEqual(policy["target_branch"], "main")
        self.assertEqual(policy["required_contexts"], EXPECTED_CONTEXTS)
        self.assertEqual(policy["merge_queue_rule"], EXPECTED_QUEUE_RULE)

    def test_every_required_context_carrier_runs_on_push_and_pull_request(self) -> None:
        required = _policy()["required_contexts"]
        assert isinstance(required, list)
        policy_contexts = set(required)
        carriers: set[str] = set()
        emitted: list[str] = []

        for workflow in _workflow_paths():
            policy_names = [name for name in _job_names(workflow) if name in policy_contexts]
            if not policy_names:
                continue
            carriers.add(workflow.name)
            emitted.extend(policy_names)
            triggers = _triggers(workflow)
            self.assertEqual(triggers["push"], {"branches": ["main"]})
            self.assertEqual(triggers["pull_request"], {"branches": ["main"]})
            # Parity: every required carrier must also run on merge_group
            # so the merge queue validates the same contexts as PR.
            self.assertIn("merge_group", triggers)
            self.assertEqual(triggers["merge_group"], {"types": ["checks_requested"]})

        self.assertEqual(carriers, EXPECTED_REQUIRED_WORKFLOWS)
        self.assertEqual(set(emitted), policy_contexts)
        emitters_by_context = {
            context: _context_emitters(context) for context in sorted(policy_contexts)
        }
        duplicate_emitters = {
            context: emitters
            for context, emitters in emitters_by_context.items()
            if len(emitters) > 1
        }
        self.assertEqual(
            duplicate_emitters,
            {},
            "each required context must have exactly one emitter; duplicates: "
            f"{duplicate_emitters}",
        )
        self.assertEqual(emitters_by_context["test"], EXPECTED_TEST_EMITTERS)

    def test_expected_workflows_handle_merge_groups(self) -> None:
        merge_group_workflows = {
            workflow.name
            for workflow in _workflow_paths()
            if "merge_group" in _triggers(workflow)
        }

        self.assertEqual(merge_group_workflows, EXPECTED_MERGE_GROUP_WORKFLOWS)

    def test_merge_queue_parity_no_dedicated_smoke_workflow(self) -> None:
        # Parity design: no dedicated smoke workflow. Full CI runs on
        # both pull_request and merge_group, so queue and PR are identical.
        smoke = WORKFLOWS_DIR / "merge-queue-smoke.yml"
        self.assertFalse(smoke.exists(), "merge-queue-smoke.yml must be deleted for parity — full CI now runs on merge_group")

        # All expected merge_group workflows must emit required contexts or be
        # part of the parity set; verify no workflow is pull_request-only.
        for workflow in _workflow_paths():
            triggers = _triggers(workflow)
            if "pull_request" in triggers and workflow.name != "release.yml" and workflow.name != "ruleset-audit.yml":
                # release is tag-only, ruleset-audit is audit-only; exclude them
                # from strict parity but all other PR workflows must have merge_group
                if workflow.name in EXPECTED_MERGE_GROUP_WORKFLOWS or workflow.name in EXPECTED_REQUIRED_WORKFLOWS:
                    self.assertIn("merge_group", triggers, f"{workflow.name} handles pull_request but not merge_group — breaks parity")

    def test_integration_context_waits_for_actual_suite(self) -> None:
        integration_workflow = WORKFLOWS_DIR / "integration-tests.yml"
        required_workflow = WORKFLOWS_DIR / "_required.yml"

        self.assertEqual(
            _context_emitters("integration-tests"),
            ["integration-tests.yml:integration"],
        )
        self.assertIn("merge_group", _triggers(integration_workflow))
        self.assertEqual(_triggers(integration_workflow)["merge_group"], {"types": ["checks_requested"]})
        self.assertNotIn("integration-tests", _job_names(required_workflow))
        self.assertNotRegex(_job_section(integration_workflow, "integration"), r"(?m)^    if:")

    def test_test_context_has_one_authoritative_build_test_producer(self) -> None:
        build_test = _job_section(WORKFLOWS_DIR / "build-test.yml", "test")

        self.assertEqual(_context_emitters("test"), EXPECTED_TEST_EMITTERS)
        self.assertEqual(
            _triggers(WORKFLOWS_DIR / "build-test.yml"),
            {
                "push": {"branches": ["main"]},
                "pull_request": {"branches": ["main"]},
                "merge_group": {"types": ["checks_requested"]},
            },
        )
        self.assertIn("needs: [build-test]", build_test)
        self.assertIn("if: always()", build_test)
        self.assertNotRegex(build_test, r"github\.event_name")
        self.assertIn("RESULT: ${{ needs.build-test.result }}", build_test)
        self.assertIn('if [ "${RESULT}" != "success" ]', build_test)
        self.assertNotIn("Skip", build_test)

    def test_docs_preserve_build_and_integration_context_semantics(self) -> None:
        document = DOC_PATH.read_text()

        self.assertIn("single authoritative `test` context", document)
        self.assertIn("Build\nand Test workflow", document)
        self.assertIn("`integration-tests`\ncontext", document)
        self.assertIn("does not emit another same-named `test` job", document)

    def test_required_workflow_has_one_authoritative_direct_graph(self) -> None:
        required_workflow = WORKFLOWS_DIR / "_required.yml"
        required_check_workflows = [
            name
            for workflow in _workflow_paths()
            if (name := _workflow_display_name(workflow)) is not None
        ]

        self.assertEqual(
            _required_graph_violations(
                required_workflow.read_text(), required_check_workflows
            ),
            [],
        )

    def test_workflow_inventory_rejects_yaml_duplicate_required_checks(self) -> None:
        required_workflow = WORKFLOWS_DIR / "_required.yml"
        with tempfile.TemporaryDirectory() as temporary_directory:
            workflow_directory = Path(temporary_directory)
            (workflow_directory / "required.yml").write_text(
                "name: Required Checks\n", encoding="utf-8"
            )
            (workflow_directory / "duplicate.yaml").write_text(
                "name: Required Checks\n", encoding="utf-8"
            )
            names = [
                name
                for workflow in _workflow_paths(workflow_directory)
                if (name := _workflow_display_name(workflow)) is not None
            ]

        violations = _required_graph_violations(
            required_workflow.read_text(), names
        )
        self.assertIn("duplicate workflow entry point", "\n".join(violations))

    def test_required_workflow_rejects_fail_open_topologies(self) -> None:
        required_workflow = WORKFLOWS_DIR / "_required.yml"
        valid = required_workflow.read_text()
        workflow_names = ["Required Checks"]
        validator_step = (
            "      - name: schema-validation (in container)\n"
            "        run: bash scripts/run_ci_local.sh schema-validation"
        )
        build_step = (
            "      - name: Build CI image (podman)\n"
            "        run: podman build --ignorefile ci/.dockerignore "
            "-f ci/Containerfile -t charybdis-ci:local ."
        )
        default_gitleaks_command = (
            "              gitleaks detect --source /workspace \\\n"
        )
        configured_gitleaks_command = (
            "              gitleaks detect --source /workspace "
            "--config .gitleaks.toml \\\n"
        )
        gitleaks_report_command = (
            "                --report-format sarif "
            "--report-path gitleaks.sarif --redact"
        )
        configured_gitleaks_branch = (
            configured_gitleaks_command + gitleaks_report_command
        )
        default_gitleaks_branch = (
            default_gitleaks_command + gitleaks_report_command
        )
        configured_gitleaks_invocation = (
            "            podman run --rm --userns=keep-id:uid=1000,gid=1000 \\\n"
            "              -v \"$PWD:/workspace:Z\" -w /workspace "
            "charybdis-ci:local \\\n"
            + configured_gitleaks_branch
        )
        default_gitleaks_invocation = (
            "            podman run --rm --userns=keep-id:uid=1000,gid=1000 \\\n"
            "              -v \"$PWD:/workspace:Z\" -w /workspace "
            "charybdis-ci:local \\\n"
            + default_gitleaks_branch
        )
        hollow_default_gitleaks = valid.replace(
            default_gitleaks_command,
            "              echo 'gitleaks detect --source /workspace' \\\n",
            1,
        )
        moved_default_gitleaks = valid.replace(
            default_gitleaks_invocation,
            "            echo 'default Gitleaks branch hollow'",
            1,
        ).replace(
            configured_gitleaks_invocation,
            configured_gitleaks_invocation + "\n" + default_gitleaks_invocation,
            1,
        )
        self.assertIn(validator_step, valid)
        self.assertIn(build_step, valid)
        self.assertIn(default_gitleaks_command, valid)
        self.assertIn(configured_gitleaks_branch, valid)
        self.assertIn(default_gitleaks_branch, valid)
        self.assertIn(configured_gitleaks_invocation, valid)
        self.assertIn(default_gitleaks_invocation, valid)
        self.assertFalse((REPO_ROOT / ".gitleaks.toml").exists())
        self.assertNotIn(default_gitleaks_command, hollow_default_gitleaks)
        self.assertIn(configured_gitleaks_command, hollow_default_gitleaks)
        self.assertEqual(moved_default_gitleaks.count(default_gitleaks_invocation), 1)
        self.assertIn("default Gitleaks branch hollow", moved_default_gitleaks)
        fixtures = {
            "missing validator": (
                _remove_job(valid, "schema-validation"),
                workflow_names,
                "missing validators",
            ),
            "hollow proxy": (
                valid
                + "\n  hollow-proxy:\n"
                + "    runs-on: ubuntu-24.04\n"
                + "    steps:\n"
                + "      - run: echo pass\n",
                workflow_names,
                "hollow or duplicate jobs",
            ),
            "hollow expected validator": (
                _replace_job(
                    valid,
                    "schema-validation",
                    "  schema-validation:\n"
                    "    name: schema-validation\n"
                    "    runs-on: ubuntu-24.04\n"
                    "    steps:\n"
                    "      - uses: actions/checkout@"
                    "3d3c42e5aac5ba805825da76410c181273ba90b1\n"
                    "      - run: echo pass\n",
                ),
                workflow_names,
                "validator command missing",
            ),
            "event-gated validator step": (
                valid.replace(
                    validator_step,
                    "      - name: schema-validation (in container)\n"
                    "        if: github.event_name == 'pull_request'\n"
                    "        run: bash scripts/run_ci_local.sh schema-validation",
                    1,
                ),
                workflow_names,
                "validator step condition not allowlisted",
            ),
            "job continue-on-error": (
                _add_job_setting(
                    valid,
                    "schema-validation",
                    "    continue-on-error: true\n",
                ),
                workflow_names,
                "job continue-on-error forbidden",
            ),
            "step continue-on-error": (
                valid.replace(
                    validator_step,
                    "      - name: schema-validation (in container)\n"
                    "        continue-on-error: true\n"
                    "        run: bash scripts/run_ci_local.sh schema-validation",
                    1,
                ),
                workflow_names,
                "step continue-on-error forbidden",
            ),
            "event-gated setup step with condition first": (
                valid.replace(
                    build_step,
                    "      - if: github.event_name == 'pull_request'\n"
                    "        name: Build CI image (podman)\n"
                    "        run: podman build --ignorefile ci/.dockerignore "
                    "-f ci/Containerfile -t charybdis-ci:local .",
                    1,
                ),
                workflow_names,
                "step condition not allowlisted",
            ),
            "echoed validator command": (
                valid.replace(
                    validator_step,
                    "      - name: schema-validation (in container)\n"
                    "        run: |\n"
                    "          echo 'bash scripts/run_ci_local.sh "
                    "schema-validation'",
                    1,
                ),
                workflow_names,
                "validator command missing",
            ),
            "commented validator command": (
                valid.replace(
                    validator_step,
                    "      - name: schema-validation (in container)\n"
                    "        run: |\n"
                    "          # bash scripts/run_ci_local.sh schema-validation\n"
                    "          echo pass",
                    1,
                ),
                workflow_names,
                "validator command missing",
            ),
            "validator command in a different step": (
                valid.replace(
                    validator_step,
                    "      - name: schema-validation (in container)\n"
                    "        run: echo pass\n"
                    "      - name: decoy schema validator\n"
                    "        run: bash scripts/run_ci_local.sh schema-validation",
                    1,
                ),
                workflow_names,
                "validator command missing",
            ),
            "hollow live default Gitleaks branch": (
                hollow_default_gitleaks,
                workflow_names,
                "validator command missing",
            ),
            "default Gitleaks command moved into configured branch": (
                moved_default_gitleaks,
                workflow_names,
                "validator command missing",
            ),
            "configured Gitleaks branch suppresses failure": (
                valid.replace(
                    configured_gitleaks_branch,
                    configured_gitleaks_branch + " || true",
                    1,
                ),
                workflow_names,
                "validator command missing",
            ),
            "default Gitleaks branch suppresses failure": (
                valid.replace(
                    default_gitleaks_branch,
                    default_gitleaks_branch + " || true",
                    1,
                ),
                workflow_names,
                "validator command missing",
            ),
            "bare dependency": (
                _add_job_setting(
                    valid,
                    "schema-validation",
                    "    needs: [upstream]\n",
                ),
                workflow_names,
                "independent validator declares needs",
            ),
            "cancelled dependency pass-through": (
                _add_job_setting(
                    valid,
                    "schema-validation",
                    "    needs: [upstream]\n    if: always()\n",
                ),
                workflow_names,
                "conditional job can mask a result",
            ),
            "unexpected skipped dependency": (
                _add_job_setting(
                    valid,
                    "schema-validation",
                    "    needs: [upstream]\n"
                    "    if: needs.upstream.result == 'success'\n",
                ),
                workflow_names,
                "conditional job can mask a result",
            ),
            "duplicate execution path": (
                valid,
                ["Required Checks", "Required Checks"],
                "duplicate workflow entry point",
            ),
        }

        for case, (workflow, names, expected) in fixtures.items():
            with self.subTest(case=case):
                violations = _required_graph_violations(workflow, names)
                self.assertIn(expected, "\n".join(violations))

    def test_required_secret_scan_keeps_scoped_sarif_permissions(self) -> None:
        required_workflow = WORKFLOWS_DIR / "_required.yml"
        workflow_text = required_workflow.read_text()
        secrets_scan = _job_section(required_workflow, "security-secrets-scan")

        self.assertNotIn(
            "security-events: write",
            workflow_text[: workflow_text.index("jobs:")],
        )
        self.assertRegex(
            secrets_scan,
            r"(?ms)^    permissions:\n      contents: read\n"
            r"      security-events: write$",
        )
        self.assertIn(
            "if: always() && hashFiles('gitleaks.sarif') != '' "
            "&& github.event_name != 'merge_group'",
            secrets_scan,
        )

    def test_release_publisher_remains_tag_and_manual_only(self) -> None:
        triggers = _triggers(WORKFLOWS_DIR / "release.yml")

        self.assertEqual(set(triggers), {"push", "workflow_dispatch"})
        self.assertEqual(triggers["push"], {"tags": ["v*.*.*"]})

    def test_readiness_doc_keeps_activation_with_odysseus(self) -> None:
        document = DOC_PATH.read_text()

        self.assertIn("configs/github/merge-queue-policy.json", document)
        self.assertIn("Odysseus", document)
        self.assertIn("must not mutate", document)
        self.assertIn("independent human", document)

    def test_required_fan_ins_run_after_real_upstream_results(self) -> None:
        for context, (workflow_name, job_id) in EXPECTED_FAN_IN_EMITTERS.items():
            workflow = WORKFLOWS_DIR / workflow_name
            section = _job_section(workflow, job_id)

            self.assertEqual(
                _context_emitters(context),
                [f"{workflow_name}:{job_id}"],
            )
            self.assertIn("merge_group", _triggers(workflow))
            self.assertEqual(_triggers(workflow)["merge_group"], {"types": ["checks_requested"]})
            self.assertRegex(section, r"(?m)^    needs:")
            self.assertRegex(section, r"(?m)^    if: always\(\)$")
            self.assertIn("needs.", section)
            self.assertIn("result", section)
            self.assertNotRegex(section, r"(?m)^    if: >")
            self.assertNotIn("Skip", section)

    def test_aggregate_fan_ins_have_explicit_result_backed_contracts(self) -> None:
        expected = {
            "lint": ("static-analysis.yml", "lint", "[clang-format, clang-tidy, action-pins, markdown-lint]"),
            "package": ("container.yml", "package", "[docker]"),
        }

        for context, (workflow_name, job_id, needs) in expected.items():
            section = _job_section(WORKFLOWS_DIR / workflow_name, job_id)
            self.assertEqual(
                _context_emitters(context), [f"{workflow_name}:{job_id}"]
            )
            self.assertIn(f"needs: {needs}", section)
            self.assertIn("if: always()", section)
            self.assertIn("needs.", section)
            self.assertIn("result", section)
            self.assertNotIn("Skip", section)

    def test_container_publish_permission_is_not_granted_to_merge_groups(self) -> None:
        workflow = WORKFLOWS_DIR / "container.yml"
        workflow_text = workflow.read_text()
        docker = _job_section(workflow, "docker")
        publish = _job_section(workflow, "publish")

        self.assertNotIn("packages: write", workflow_text[: workflow_text.index("jobs:")])
        self.assertNotIn("packages: write", docker)
        self.assertIn(
            "if: github.event_name == 'push' && github.ref == 'refs/heads/main'",
            publish,
        )
        self.assertRegex(
            publish,
            r"(?ms)^    permissions:\n      contents: read\n      packages: write$",
        )

    def test_secrets_scan_job_grants_security_events_write_for_sarif_upload(
        self,
    ) -> None:
        workflow = WORKFLOWS_DIR / "_required.yml"
        workflow_text = workflow.read_text()

        # No workflow-level security-events grant — least privilege is scoped
        # to the one job that uploads SARIF.
        self.assertNotIn(
            "security-events: write", workflow_text[: workflow_text.index("jobs:")]
        )
        secrets_scan = _job_section(workflow, "security-secrets-scan")
        self.assertIn("Upload Gitleaks SARIF", secrets_scan)
        self.assertRegex(
            secrets_scan,
            r"(?ms)^    permissions:\n      contents: read\n      security-events: write$",
        )

    def test_required_proxy_workflow_does_not_emit_fan_in_contexts(self) -> None:
        required_workflow = WORKFLOWS_DIR / "_required.yml"
        required_contexts = set(EXPECTED_FAN_IN_EMITTERS)

        self.assertTrue(required_contexts.isdisjoint(_job_names(required_workflow)))

    def test_policy_regression_is_wired_into_authoritative_validation(self) -> None:
        justfile = JUSTFILE_PATH.read_text()
        precommit = PRECOMMIT_PATH.read_text()
        required_workflow = _job_section(
            WORKFLOWS_DIR / "_required.yml", "schema-validation"
        )

        self.assertRegex(
            justfile,
            r"(?ms)^merge-queue-policy:\n\s+\./scripts/test-merge-queue-policy\.py\s*$",
        )
        self.assertRegex(justfile, r"(?m)^ci:.*\bmerge-queue-policy\b")
        self.assertRegex(
            precommit,
            r"(?ms)^      - id: merge-queue-policy\n.*?^        entry: \.?/scripts/test-merge-queue-policy\.py$",
        )
        self.assertIn("python3 scripts/test-merge-queue-policy.py", required_workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
