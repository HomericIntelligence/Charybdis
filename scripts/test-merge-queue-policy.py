#!/usr/bin/env python3
"""Behavioral regression tests for staged merge-queue readiness."""

from __future__ import annotations

import json
import re
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

EXPECTED_MERGE_GROUP_WORKFLOWS = EXPECTED_REQUIRED_WORKFLOWS

EXPECTED_FAN_IN_EMITTERS = {
    "build": ("build-test.yml", "build"),
    "unit-tests": ("build-test.yml", "unit-tests"),
    "lint": ("static-analysis.yml", "lint"),
    "package": ("container.yml", "package"),
}

EXPECTED_TEST_EMITTERS = [
    "build-test.yml:test",
]


def _inline_list(value: str) -> list[str]:
    """Parse the simple inline YAML lists used by workflow trigger blocks."""
    match = re.fullmatch(r"\[(.*)]", value.strip())
    if not match:
        raise AssertionError(f"expected an inline YAML list, got {value!r}")
    body = match.group(1).strip()
    if not body:
        return []
    return [item.strip().strip("'\"") for item in body.split(",")]


def _triggers(path: Path) -> dict[str, dict[str, list[str]]]:
    """Read top-level Actions events and their inline list settings."""
    lines = path.read_text().splitlines()
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


def _job_contexts(path: Path) -> list[tuple[str, str]]:
    """Return each job id and the check context it emits."""
    lines = path.read_text().splitlines()
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


def _job_section(path: Path, job_id: str) -> str:
    """Return one job definition from a workflow's jobs section."""
    lines = path.read_text().splitlines()
    start = lines.index("jobs:") + 1
    job_line = f"  {job_id}:"
    job_start = next(index for index in range(start, len(lines)) if lines[index] == job_line)
    job_end = len(lines)
    for index in range(job_start + 1, len(lines)):
        if re.match(r"^  [A-Za-z0-9_-]+:\s*$", lines[index]):
            job_end = index
            break
    return "\n".join(lines[job_start:job_end])


def _job_names(path: Path) -> list[str]:
    """Return job check contexts, including ids without an explicit name."""
    return [name for _, name in _job_contexts(path)]


def _context_emitters(context: str) -> list[str]:
    """Return workflow/job emitters for a required check context."""
    emitters: list[str] = []
    for workflow in sorted(WORKFLOWS_DIR.glob("*.yml")):
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

    def test_every_required_context_carrier_handles_merge_groups(self) -> None:
        policy_contexts = set(_policy()["required_contexts"])
        carriers: set[str] = set()
        emitted: list[str] = []

        for workflow in sorted(WORKFLOWS_DIR.glob("*.yml")):
            policy_names = [name for name in _job_names(workflow) if name in policy_contexts]
            if not policy_names:
                continue
            carriers.add(workflow.name)
            emitted.extend(policy_names)
            triggers = _triggers(workflow)
            self.assertEqual(triggers["push"], {"branches": ["main"]})
            self.assertEqual(triggers["pull_request"], {"branches": ["main"]})
            self.assertEqual(
                triggers["merge_group"], {"types": ["checks_requested"]}
            )

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
            for workflow in WORKFLOWS_DIR.glob("*.yml")
            if "merge_group" in _triggers(workflow)
        }

        self.assertEqual(merge_group_workflows, EXPECTED_MERGE_GROUP_WORKFLOWS)

    def test_merge_group_integration_context_waits_for_actual_suite(self) -> None:
        integration_workflow = WORKFLOWS_DIR / "integration-tests.yml"
        required_workflow = WORKFLOWS_DIR / "_required.yml"

        self.assertEqual(
            _context_emitters("integration-tests"),
            ["integration-tests.yml:integration"],
        )
        self.assertEqual(
            _triggers(integration_workflow)["merge_group"],
            {"types": ["checks_requested"]},
        )
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

    def test_required_workflow_preserves_workflow_run_fan_in(self) -> None:
        triggers = _triggers(WORKFLOWS_DIR / "_required.yml")

        self.assertEqual(
            set(triggers), {"workflow_run", "pull_request", "push", "merge_group"}
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
