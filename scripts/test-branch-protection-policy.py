#!/usr/bin/env python3
"""Behavioral regression tests for the branch-protection governance policy (#101).

Asserts that `apply-branch-protection.sh` clears `bypass_actors` to `[]`
instead of round-tripping whatever the live ruleset returns, that both the
script and the `Ruleset Audit` workflow fail closed when a bypass actor
reappears, and that the governance decision is documented for maintainers.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "scripts" / "apply-branch-protection.sh"
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "ruleset-audit.yml"
DOC_PATH = REPO_ROOT / "CONTRIBUTING.md"


def _payload_block(script: str) -> str:
    """Return the jq PUT-payload block from apply-branch-protection.sh."""
    match = re.search(r"PAYLOAD=\$\(gh api.*?jq '(?s:(.*?)?)'\)", script)
    assert match is not None, "PAYLOAD jq block not found in script"
    return match.group(1)


class BranchProtectionPolicyTests(unittest.TestCase):
    def test_payload_clears_bypass_actors_instead_of_passing_through(self) -> None:
        script = SCRIPT_PATH.read_text()
        payload = _payload_block(script)

        # The pipeline must force an empty bypass list before projecting the
        # PUT payload fields.
        self.assertIn(".bypass_actors = []", payload)

        # A pass-through would project bypass_actors into the payload without
        # clearing it first; the clearing clause must precede the projection.
        clear_at = payload.index(".bypass_actors = []")
        project_at = payload.index("{name, target, enforcement, conditions, rules, bypass_actors}")
        self.assertLess(clear_at, project_at)

    def test_script_verifies_empty_bypass_actors_after_apply(self) -> None:
        section = SCRIPT_PATH.read_text()

        self.assertRegex(
            section,
            r"BYPASS_LEN=\$\(gh api [\s\S]*?--jq '\.bypass_actors \| length'\)",
            "script must read back bypass_actors after the PUT",
        )
        self.assertIn('if [ "${BYPASS_LEN}" != "0" ]; then', section)
        self.assertRegex(section, r'FAIL: bypass_actors length is')
        self.assertRegex(
            section,
            r"OK: bypass_actors=\[\] confirmed on",
        )

    def test_audit_workflow_asserts_bypass_actors_are_empty(self) -> None:
        workflow = WORKFLOW_PATH.read_text()

        self.assertIn("name: Assert bypass_actors is empty", workflow)
        self.assertRegex(
            workflow,
            r"--jq '\.bypass_actors \| length'",
            "audit step must query the live ruleset's bypass_actors",
        )
        self.assertIn('if [ "${BYPASS_LEN}" != "0" ]; then', workflow)
        self.assertIn("exit 1", workflow)
        self.assertRegex(workflow, r"Restore via: scripts/apply-branch-protection\.sh")
        # The guard must be documented as guarding issue #101's invariant.
        self.assertIn("#101", workflow)

    def test_contributing_docs_describe_bypass_removal(self) -> None:
        document = DOC_PATH.read_text()

        self.assertIn("bypass_actors", document)
        self.assertIn("[]", document)
        self.assertIn("no actor", document.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
