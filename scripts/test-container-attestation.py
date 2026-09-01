#!/usr/bin/env python3
"""Structural regression tests for container image SBOM + provenance attestation.

Parses `.github/workflows/release.yml` with `yaml.safe_load` and navigates to
the specific job/step before asserting each attestation contract (#108). A
regex over the raw YAML text would pass even if a flag migrated into the
wrong step or job; structural navigation fails in that case.

Run directly (no pytest dependency), mirroring `scripts/test-merge-queue-policy.py`.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path
from typing import Any

import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]
RELEASE_WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "release.yml"

CONTAINER_JOB_ID = "build-container"
SUBJECT_NAME = "ghcr.io/${{ github.repository }}"
DIGEST_EXPR = "${{ steps.push.outputs.digest }}"


def _release_workflow() -> dict[str, Any]:
    """Load release.yml as a plain dict via yaml.safe_load."""
    return yaml.safe_load(RELEASE_WORKFLOW_PATH.read_text(encoding="utf-8"))


def _container_job() -> dict[str, Any]:
    """Return the `build-container` job definition."""
    jobs = _release_workflow()["jobs"]
    assert CONTAINER_JOB_ID in jobs, (
        f"{RELEASE_WORKFLOW_PATH.name} must define a '{CONTAINER_JOB_ID}' job"
    )
    return jobs[CONTAINER_JOB_ID]


def _steps() -> list[dict[str, Any]]:
    """Return the step list of the `build-container` job."""
    steps = _container_job()["steps"]
    assert isinstance(steps, list) and steps, (
        f"'{CONTAINER_JOB_ID}' must declare a non-empty steps list"
    )
    return [step for step in steps if isinstance(step, dict)]


def _find_step(uses_prefix: str) -> dict[str, Any]:
    """Return the first step whose `uses:` starts with the given prefix."""
    for step in _steps():
        uses = step.get("uses", "")
        if isinstance(uses, str) and uses.startswith(uses_prefix):
            return step
    raise AssertionError(
        f"no step using '{uses_prefix}*' found in '{CONTAINER_JOB_ID}'"
    )


def _with(step: dict[str, Any]) -> dict[str, Any]:
    """Return a step's `with:` block as a dict."""
    with_block = step.get("with")
    assert isinstance(with_block, dict), (
        f"step {step.get('name')!r} lacks a with: block"
    )
    return with_block


class ContainerAttestationTests(unittest.TestCase):
    """Structural assertions on the release workflow's attestation wiring."""

    def test_container_job_has_id_token_and_attestations_write(self) -> None:
        perms = _container_job()["permissions"]
        self.assertEqual(
            perms.get("id-token"),
            "write",
            "build-container must declare 'id-token: write' for sigstore OIDC signing",
        )
        self.assertEqual(
            perms.get("attestations"),
            "write",
            "build-container must declare 'attestations: write' to store attestations",
        )

    def test_multi_arch_push_step_is_id_push_with_push_true(self) -> None:
        candidates = [
            step
            for step in _steps()
            if isinstance(step.get("uses"), str)
            and step["uses"].startswith("docker/build-push-action@")
            and _with(step).get("push") is True
        ]
        assert candidates, (
            f"'{CONTAINER_JOB_ID}' must contain a docker/build-push-action "
            "step with 'push: true'"
        )
        step = candidates[0]
        self.assertEqual(
            step.get("id"),
            "push",
            "the multi-arch push step must have 'id: push' so its "
            "digest output is addressable",
        )

    def test_build_provenance_attests_pushed_digest(self) -> None:
        with_block = _with(_find_step("actions/attest-build-provenance@"))
        self.assertEqual(with_block.get("subject-name"), SUBJECT_NAME)
        self.assertEqual(
            with_block.get("subject-digest"),
            DIGEST_EXPR,
            "provenance must bind the pushed manifest-list digest "
            "(steps.push.outputs.digest)",
        )
        self.assertIs(
            with_block.get("push-to-registry"),
            True,
            "push-to-registry must be true so the attestation is "
            "attached to the GHCR image and visible to "
            "'gh attestation verify oci://...'",
        )

    def test_sbom_step_generates_spdx_json_file(self) -> None:
        step = _find_step("anchore/sbom-action@")
        with_block = _with(step)
        self.assertEqual(with_block.get("format"), "spdx-json")
        output_file = with_block.get("output-file")
        assert isinstance(output_file, str)
        self.assertTrue(
            output_file.endswith(".spdx.json"),
            f"SBOM output-file must be an SPDX JSON path, got {output_file!r}",
        )

    def test_sbom_attestation_binds_same_digest_and_sbom_file(self) -> None:
        attest_with = _with(_find_step("actions/attest-sbom@"))
        sbom_with = _with(_find_step("anchore/sbom-action@"))
        self.assertEqual(attest_with.get("subject-name"), SUBJECT_NAME)
        self.assertEqual(attest_with.get("subject-digest"), DIGEST_EXPR)
        self.assertEqual(
            attest_with.get("sbom-file-path"),
            sbom_with.get("output-file"),
            "attest-sbom must consume exactly the file the SBOM step wrote",
        )
        self.assertIs(
            attest_with.get("push-to-registry"),
            True,
            "push-to-registry must be true so the SBOM attestation "
            "is attached to the GHCR image and visible to "
            "'gh attestation verify oci://...'",
        )

    def test_every_release_workflow_action_ref_is_sha_pinned(self) -> None:
        unpinned: list[str] = []
        for index, line in enumerate(
            RELEASE_WORKFLOW_PATH.read_text(encoding="utf-8").splitlines(), start=1
        ):
            stripped = line.strip()
            match = re.match(r"(?:-\s+)?uses:\s*(\S+)\s*#?\s*(.*)$", stripped)
            if not match:
                continue
            ref, comment = match.group(1), match.group(2)
            if ref.startswith("./"):
                continue  # local composite actions are exempt
            if not re.fullmatch(r"[0-9a-f]{40}", ref.rsplit("@", 1)[-1]) or not (
                comment.startswith("v")
            ):
                unpinned.append(f"{RELEASE_WORKFLOW_PATH.name}:{index}: {stripped}")
        self.assertEqual(
            unpinned,
            [],
            "every release.yml uses: must be pinned to a 40-char "
            "SHA with a '# v<tag>' comment",
        )


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ContainerAttestationTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
