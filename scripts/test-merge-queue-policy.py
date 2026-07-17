#!/usr/bin/env python3
"""Behavioral regression tests for staged merge-queue readiness."""

from __future__ import annotations

import json
import re
import unittest
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS_DIR = REPO_ROOT / ".github" / "workflows"
POLICY_PATH = REPO_ROOT / "configs" / "github" / "merge-queue-policy.json"
DOC_PATH = REPO_ROOT / "docs" / "ci" / "merge-queue.md"

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
    "integration-tests.yml",
    "static-analysis.yml",
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


def _job_names(path: Path) -> list[str]:
    """Return explicit job check names from a workflow."""
    lines = path.read_text().splitlines()
    start = lines.index("jobs:") + 1
    names: list[str] = []
    in_job = False

    for line in lines[start:]:
        if line and not line.startswith((" ", "#")):
            break
        if re.match(r"^  [A-Za-z0-9_-]+:\s*$", line):
            in_job = True
            continue
        name_match = re.match(r"^    name:\s*(.+?)\s*$", line)
        if in_job and name_match:
            names.append(name_match.group(1).strip("'\""))

    return names


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
        self.assertEqual(
            {name: count for name, count in Counter(emitted).items() if count > 1},
            {"integration-tests": 2},
        )

    def test_only_required_context_carriers_handle_merge_groups(self) -> None:
        merge_group_workflows = {
            workflow.name
            for workflow in WORKFLOWS_DIR.glob("*.yml")
            if "merge_group" in _triggers(workflow)
        }

        self.assertEqual(merge_group_workflows, EXPECTED_REQUIRED_WORKFLOWS)

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
