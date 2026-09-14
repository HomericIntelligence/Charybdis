#!/usr/bin/env python3
"""Behavioral regression tests for static-analysis placement (#373).

`Charybdis_ENABLE_CLANG_TIDY` and `Charybdis_ENABLE_CPPCHECK` used to default to
`ON`, so *every* configuration that ran `cmake --preset` without opting out
re-ran clang-tidy over every translation unit -- the build-test matrix (4 legs),
install, asan/tsan, coverage, codeql, integration-tests and release -- while the
dedicated `clang-tidy` job, gated by the required `lint` aggregate, already
existed to do exactly that.

These tests pin the corrected contract:

  * both options default to `OFF`, so no build configuration inherits them;
  * the only place either analyzer is switched on is its own job inside the
    dedicated Static Analysis workflow;
  * the required `lint` and `All Static Analysis Checks` aggregates gate on both
    analyzer jobs;
  * cppcheck's argument list forms a *real* gate (#375): `--error-exitcode=1` is
    present (CMake never adds it, so without it a required job passes while
    finding nothing fatal), `--library=googletest` is loaded (without it cppcheck
    aborts every gtest translation unit at the second `TEST()` with a syntax
    error and analyzes none of it), and the globally suppressed id set is exactly
    the documented baseline.

The negative cases at the bottom mutate the real file contents and assert the
validators reject them, so the suite cannot silently become a no-op.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS_DIR = REPO_ROOT / ".github" / "workflows"
CMAKE_MODULE_PATH = REPO_ROOT / "cmake" / "StaticAnalyzers.cmake"
JUSTFILE_PATH = REPO_ROOT / "justfile"
PRECOMMIT_PATH = REPO_ROOT / ".pre-commit-config.yaml"
REQUIRED_WORKFLOW_PATH = WORKFLOWS_DIR / "_required.yml"

STATIC_ANALYSIS_WORKFLOW = "static-analysis.yml"

# analyzer option suffix -> the job that is allowed to switch it on
ANALYZER_JOBS = {
    "CLANG_TIDY": "clang-tidy",
    "CPPCHECK": "cppcheck",
}

# aggregates that must gate on both analyzer jobs
GATES = {
    "lint": "lint",
    "All Static Analysis Checks": "check-all",
}


_JOB_ID_RE = re.compile(r"^  ([A-Za-z0-9_.-]+):\s*$")
_OPTION_DEFAULT_RE = re.compile(
    r"option\(\s*\$\{PROJECT_NAME\}_ENABLE_(CLANG_TIDY|CPPCHECK)\s+\"[^\"]*\"\s+(ON|OFF)\s*\)",
    re.S,
)
_ENABLE_RE = re.compile(r"Charybdis_ENABLE_(CLANG_TIDY|CPPCHECK)\s*=\s*ON\b")
_SET_CPPCHECK_RE = re.compile(r"set\(CMAKE_CXX_CPPCHECK(.*?)\)", re.S)

# Flags cppcheck's argument list must carry. `--error-exitcode=1` is the whole point:
# CMake's CMAKE_CXX_CPPCHECK integration never adds it, so a build (and therefore the
# required `lint` aggregate) succeeds even on an `error:`-severity finding.
REQUIRED_CPPCHECK_FLAGS = {
    "--enable=all",
    "--inline-suppr",
    "--inconclusive",
    "--library=googletest",
    "--error-exitcode=1",
}

# The complete suppression baseline. Each of these cannot work under CMake's
# per-translation-unit invocation:
#   missingIncludeSystem  no -isystem paths are handed to cppcheck, so every
#                         `#include <...>` reports this; cppcheck documents that it
#                         does not need stdlib headers.
#   unusedFunction        whole-program-only check; per-TU every function used from
#                         another TU is a false positive, and googletest.cfg defines
#                         each TEST body as a standalone function.
#   unmatchedSuppression  a suppression is "unused" in any TU with no finding of that
#                         id, which itself reports as a finding.
EXPECTED_CPPCHECK_SUPPRESSIONS = {
    "missingIncludeSystem",
    "unusedFunction",
    "unmatchedSuppression",
}


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def cppcheck_args(text: str) -> list[str]:
    """Every argv entry in ``set(CMAKE_CXX_CPPCHECK ...)``, with comments stripped.

    The ``${CPPCHECK}`` placeholder is dropped: the test pins the arguments around it.
    """
    match = _SET_CPPCHECK_RE.search(text)
    if not match:
        return []
    args: list[str] = []
    for line in match.group(1).splitlines():
        uncommented = line.split("#", 1)[0].strip()
        args.extend(uncommented.split())
    return [arg for arg in args if not arg.startswith("${")]


def cppcheck_suppressions(text: str) -> set[str]:
    return {
        arg.split("=", 1)[1]
        for arg in cppcheck_args(text)
        if arg.startswith("--suppress=")
    }


def cppcheck_gate_violations(text: str) -> list[str]:
    """The cppcheck arguments must form a gate whose suppression list is the baseline."""
    args = cppcheck_args(text)
    if not args:
        return [f"{CMAKE_MODULE_PATH.name}: no set(CMAKE_CXX_CPPCHECK ...) argument list"]

    violations = [
        f"{CMAKE_MODULE_PATH.name}: cppcheck is missing {flag}"
        for flag in sorted(REQUIRED_CPPCHECK_FLAGS)
        if flag not in args
    ]

    suppressions = cppcheck_suppressions(text)
    violations += [
        f"{CMAKE_MODULE_PATH.name}: unexpected cppcheck suppression '{name}'"
        for name in sorted(suppressions - EXPECTED_CPPCHECK_SUPPRESSIONS)
    ]
    violations += [
        f"{CMAKE_MODULE_PATH.name}: cppcheck suppression '{name}' is missing"
        for name in sorted(EXPECTED_CPPCHECK_SUPPRESSIONS - suppressions)
    ]

    # `missingInclude` (the pre-#375 entry) suppresses the *project*-header id, which
    # never fires here because the build passes -I include and -I build/debug/include,
    # so it matched nothing and reported unmatchedSuppression instead. cppcheck 2.x
    # reports system headers under the separate `missingIncludeSystem` id.
    if "missingInclude" in suppressions:
        violations.append(
            f"{CMAKE_MODULE_PATH.name}: --suppress=missingInclude matches nothing in this "
            f"build (project headers resolve); the system-header id is missingIncludeSystem"
        )
    return violations


def _jobs_block(text: str) -> str:
    """Return the text of the top-level ``jobs:`` mapping."""
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if line.rstrip() == "jobs:":
            start = index + 1
            break
    if start is None:
        return ""

    end = len(lines)
    for index in range(start, len(lines)):
        line = lines[index]
        if line.strip() and not line.startswith((" ", "#")):
            end = index
            break
    return "\n".join(lines[start:end])


def _job_ids(text: str) -> list[str]:
    return [
        match.group(1)
        for match in (_JOB_ID_RE.match(line) for line in _jobs_block(text).splitlines())
        if match
    ]


def _job_section(text: str, job_id: str) -> str:
    """Return the body of ``job_id`` (its child lines, dedented by nothing)."""
    captured: list[str] = []
    capturing = False
    for line in _jobs_block(text).splitlines():
        match = _JOB_ID_RE.match(line)
        if match:
            capturing = match.group(1) == job_id
            continue
        if capturing:
            captured.append(line)
    return "\n".join(captured)


def _needs(section: str) -> list[str]:
    """Parse a job's ``needs:`` in either inline or block-list form."""
    inline = re.search(r"(?m)^    needs:\s*\[(.*?)\]\s*$", section)
    if inline:
        return [part.strip() for part in inline.group(1).split(",") if part.strip()]

    block = re.search(r"(?m)^    needs:\s*$\n((?:\s*-\s*\S+\n?)+)", section)
    if block:
        return re.findall(r"-\s*(\S+)", block.group(1))
    return []


def option_defaults(text: str) -> dict[str, str]:
    """Map analyzer suffix -> declared default ("ON"/"OFF")."""
    return {name: state for name, state in _OPTION_DEFAULT_RE.findall(text)}


def configure_invocations(text: str) -> list[tuple[str, str]]:
    """Every (job_id, line) that configures the build with a preset."""
    found: list[tuple[str, str]] = []
    for job_id in _job_ids(text):
        for line in _job_section(text, job_id).splitlines():
            if "cmake --preset" in line:
                found.append((job_id, line.strip()))
    return found


def analyzer_opt_ins(text: str) -> dict[str, list[str]]:
    """Map analyzer suffix -> job ids whose body switches it on."""
    found: dict[str, list[str]] = {}
    for job_id in _job_ids(text):
        for name in _ENABLE_RE.findall(_job_section(text, job_id)):
            found.setdefault(name, []).append(job_id)
    return found


def default_violations(text: str) -> list[str]:
    """Both analyzers must default OFF."""
    defaults = option_defaults(text)
    violations: list[str] = []
    for name in ANALYZER_JOBS:
        state = defaults.get(name)
        if state is None:
            violations.append(f"{CMAKE_MODULE_PATH.name}: no option() declares {name}")
        elif state != "OFF":
            violations.append(
                f"{CMAKE_MODULE_PATH.name}: Charybdis_ENABLE_{name} defaults to {state}, expected OFF"
            )
    return violations


def placement_violations(workflow_name: str, text: str) -> list[str]:
    """Only the analyzer's own job, in the dedicated workflow, may enable it.

    In the dedicated workflow each analyzer must be switched on by exactly its
    own job -- so a missing opt-in, a duplicated one, and one attributed to the
    wrong job are all violations.
    """
    violations: list[str] = []
    opt_ins = analyzer_opt_ins(text)

    if workflow_name != STATIC_ANALYSIS_WORKFLOW:
        for name, jobs in opt_ins.items():
            violations.append(
                f"{workflow_name}: Charybdis_ENABLE_{name}=ON in job '{jobs[0]}' — "
                f"analyzers may only be enabled by {STATIC_ANALYSIS_WORKFLOW}"
            )
        return violations

    for name, expected in ANALYZER_JOBS.items():
        jobs = opt_ins.get(name, [])
        if jobs != [expected]:
            violations.append(
                f"{workflow_name}: Charybdis_ENABLE_{name}=ON in {jobs}, expected ['{expected}']"
            )
    return violations


def gate_violations(workflow_name: str, text: str) -> list[str]:
    """The required aggregates must need and check both analyzer jobs."""
    violations: list[str] = []
    for job_id in GATES.values():
        section = _job_section(text, job_id)
        needs = _needs(section)
        for name, analyzer_job in ANALYZER_JOBS.items():
            if analyzer_job not in needs:
                violations.append(
                    f"{workflow_name}:{job_id} needs {needs} — missing analyzer job '{analyzer_job}'"
                )
            if f"needs.{analyzer_job}.result" not in section:
                violations.append(
                    f"{workflow_name}:{job_id} does not gate on needs.{analyzer_job}.result"
                )
    return violations


def _all_workflow_violations() -> dict[str, list[str]]:
    violations: dict[str, list[str]] = {}
    for path in sorted(WORKFLOWS_DIR.glob("*.yml")):
        found = placement_violations(path.name, _read(path))
        if path.name == STATIC_ANALYSIS_WORKFLOW:
            found += gate_violations(path.name, _read(path))
        if found:
            violations[path.name] = found
    return violations


class TestAnalyzerDefaults(unittest.TestCase):
    def test_both_analyzers_default_off(self) -> None:
        self.assertEqual(default_violations(_read(CMAKE_MODULE_PATH)), [])
        self.assertEqual(
            option_defaults(_read(CMAKE_MODULE_PATH)),
            {"CLANG_TIDY": "OFF", "CPPCHECK": "OFF"},
        )

    def test_module_documents_the_opt_in_contract(self) -> None:
        text = _read(CMAKE_MODULE_PATH)
        self.assertIn("#373", text)
        self.assertIn(STATIC_ANALYSIS_WORKFLOW, text)


class TestAnalyzerPlacement(unittest.TestCase):
    def test_no_build_configuration_inherits_an_analyzer(self) -> None:
        self.assertEqual(_all_workflow_violations(), {})

    def test_build_matrix_configures_without_enabling_an_analyzer(self) -> None:
        text = _read(WORKFLOWS_DIR / "build-test.yml")
        invocations = configure_invocations(text)
        self.assertTrue(invocations, "build-test.yml should configure the build")
        for job_id, line in invocations:
            self.assertNotIn("Charybdis_ENABLE_", line, f"build-test.yml:{job_id} enables an analyzer")

    def test_static_analysis_workflow_owns_both_analyzer_jobs(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        self.assertEqual(
            analyzer_opt_ins(text),
            {"CLANG_TIDY": ["clang-tidy"], "CPPCHECK": ["cppcheck"]},
        )

    def test_analyzer_jobs_opt_in_explicitly(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        for name, job_id in ANALYZER_JOBS.items():
            section = _job_section(text, job_id)
            self.assertIn(f"-DCharybdis_ENABLE_{name}=ON", section)

    def test_every_other_workflow_stays_clean(self) -> None:
        for path in sorted(WORKFLOWS_DIR.glob("*.yml")):
            if path.name == STATIC_ANALYSIS_WORKFLOW:
                continue
            self.assertEqual(
                placement_violations(path.name, _read(path)),
                [],
                f"{path.name} enables a static analyzer",
            )


class TestRequiredGates(unittest.TestCase):
    def test_required_aggregates_gate_on_both_analyzers(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        self.assertEqual(gate_violations(STATIC_ANALYSIS_WORKFLOW, text), [])

    def test_gate_needs_are_exact(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        expected = ["clang-format", "clang-tidy", "cppcheck", "action-pins", "markdown-lint"]
        for job_id in GATES.values():
            self.assertEqual(_needs(_job_section(text, job_id)), expected)


class TestPolicyWiring(unittest.TestCase):
    def test_justfile_recipe_and_ci_alias(self) -> None:
        justfile = _read(JUSTFILE_PATH)
        self.assertRegex(
            justfile,
            r"(?ms)^static-analysis-policy:\n\s+\./scripts/test-static-analysis-policy\.py\s*$",
        )
        self.assertRegex(justfile, r"(?m)^ci:.*\bstatic-analysis-policy\b")

    def test_precommit_hook_runs_the_policy_test(self) -> None:
        precommit = _read(PRECOMMIT_PATH)
        self.assertRegex(
            precommit,
            r"(?ms)^      - id: static-analysis-policy\n"
            r".*?^        entry: \.?/scripts/test-static-analysis-policy\.py$",
        )
        self.assertRegex(
            precommit,
            r"(?ms)^      - id: static-analysis-policy\n.*?^        files: .*StaticAnalyzers\\\.cmake",
        )

    def test_ci_invokes_the_policy_test(self) -> None:
        self.assertIn(
            "python3 scripts/test-static-analysis-policy.py",
            _read(REQUIRED_WORKFLOW_PATH),
        )


class TestCppcheckHardGate(unittest.TestCase):
    """cppcheck must be a gate, and must actually analyze the code (#375)."""

    def test_argument_list_satisfies_the_gate_contract(self) -> None:
        self.assertEqual(cppcheck_gate_violations(_read(CMAKE_MODULE_PATH)), [])

    def test_findings_are_fatal(self) -> None:
        args = cppcheck_args(_read(CMAKE_MODULE_PATH))
        self.assertIn("--error-exitcode=1", args)
        # Placeholder ordering matters: the flags must be cppcheck's argv, not its input.
        self.assertTrue(all(arg.startswith("--") for arg in args))

    def test_gtest_tus_are_parseable(self) -> None:
        """Without googletest.cfg cppcheck aborts on the second TEST() in every TU."""
        self.assertIn("--library=googletest", cppcheck_args(_read(CMAKE_MODULE_PATH)))

    def test_suppression_list_is_exactly_the_baseline(self) -> None:
        self.assertEqual(
            cppcheck_suppressions(_read(CMAKE_MODULE_PATH)),
            EXPECTED_CPPCHECK_SUPPRESSIONS,
        )

    def test_module_documents_the_gate(self) -> None:
        text = _read(CMAKE_MODULE_PATH)
        for token in ("#375", "--error-exitcode=1", "--library=googletest", "missingIncludeSystem"):
            self.assertIn(token, text)


class TestValidatorsRejectRegressions(unittest.TestCase):
    """Negative cases: the validators must fail on the defect this pins."""

    def test_default_flipping_back_on_is_caught(self) -> None:
        text = _read(CMAKE_MODULE_PATH)
        self.assertEqual(default_violations(text), [])
        flipped = re.sub(
            r"(option\(\s*\$\{PROJECT_NAME\}_ENABLE_CLANG_TIDY\s+\"[^\"]*\"\s+)OFF",
            r"\1ON",
            text,
        )
        self.assertNotEqual(flipped, text)
        self.assertTrue(default_violations(flipped))

    def test_missing_option_declaration_is_caught(self) -> None:
        text = _read(CMAKE_MODULE_PATH)
        removed = re.sub(r"(?m)^option\(\$\{PROJECT_NAME\}_ENABLE_CPPCHECK.*\n", "", text)
        self.assertNotEqual(removed, text)
        self.assertIn("CPPCHECK", " ".join(default_violations(removed)))

    def test_hidden_default_is_caught(self) -> None:
        """An option help string mentioning OFF must not satisfy the contract."""
        sneaky = (
            'option(${PROJECT_NAME}_ENABLE_CLANG_TIDY "Enable clang-tidy OFF by default" ON)\n'
            'option(${PROJECT_NAME}_ENABLE_CPPCHECK "Enable cppcheck OFF by default" ON)\n'
        )
        self.assertTrue(default_violations(sneaky))

    def test_build_matrix_opt_in_is_caught(self) -> None:
        text = _read(WORKFLOWS_DIR / "build-test.yml")
        self.assertEqual(placement_violations("build-test.yml", text), [])
        mutated = text.replace(
            'uv run cmake --preset "${BUILD_TYPE}"',
            'uv run cmake --preset "${BUILD_TYPE}" -DCharybdis_ENABLE_CLANG_TIDY=ON',
            1,
        )
        self.assertNotEqual(mutated, text)
        violations = placement_violations("build-test.yml", mutated)
        self.assertTrue(violations)
        self.assertIn("build-test.yml", violations[0])

    def test_opt_in_moved_into_the_wrong_job_is_caught(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        self.assertEqual(placement_violations(STATIC_ANALYSIS_WORKFLOW, text), [])

        clang_tidy_opt_in = (
            "run: CC=clang CXX=clang++ uv run cmake --preset debug "
            "-DCharybdis_ENABLE_CLANG_TIDY=ON"
        )
        cppcheck_opt_in = "run: uv run cmake --preset debug -DCharybdis_ENABLE_CPPCHECK=ON"
        self.assertIn(clang_tidy_opt_in, text)
        self.assertIn(cppcheck_opt_in, text)

        # Move the cppcheck opt-in out of its own job and into clang-tidy's.
        mutated = text.replace(clang_tidy_opt_in + "\n", clang_tidy_opt_in + " -DCharybdis_ENABLE_CPPCHECK=ON\n", 1)
        mutated = mutated.replace(cppcheck_opt_in + "\n", "run: uv run cmake --preset debug\n", 1)
        self.assertNotEqual(mutated, text)
        self.assertEqual(analyzer_opt_ins(mutated), {"CLANG_TIDY": ["clang-tidy"], "CPPCHECK": ["clang-tidy"]})
        self.assertTrue(placement_violations(STATIC_ANALYSIS_WORKFLOW, mutated))

    def test_dropped_opt_in_is_caught(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        dropped = text.replace(
            "run: uv run cmake --preset debug -DCharybdis_ENABLE_CPPCHECK=ON\n",
            "run: uv run cmake --preset debug\n",
            1,
        )
        self.assertNotEqual(dropped, text)
        violations = placement_violations(STATIC_ANALYSIS_WORKFLOW, dropped)
        self.assertTrue(violations)
        self.assertIn("CPPCHECK", " ".join(violations))

    def test_dropping_an_analyzer_from_a_gate_is_caught(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        self.assertEqual(gate_violations(STATIC_ANALYSIS_WORKFLOW, text), [])
        mutated = text.replace(
            "needs: [clang-format, clang-tidy, cppcheck, action-pins, markdown-lint]",
            "needs: [clang-format, clang-tidy, action-pins, markdown-lint]",
        )
        self.assertNotEqual(mutated, text)
        violations = gate_violations(STATIC_ANALYSIS_WORKFLOW, mutated)
        self.assertEqual(len(violations), 2, violations)  # both gates

    def test_dropping_error_exitcode_is_caught(self) -> None:
        text = _read(CMAKE_MODULE_PATH)
        self.assertEqual(cppcheck_gate_violations(text), [])
        mutated = re.sub(r"\n\s+--error-exitcode=1", "", text, count=1)
        self.assertNotEqual(mutated, text)
        violations = cppcheck_gate_violations(mutated)
        self.assertIn("--error-exitcode=1", " ".join(violations))

    def test_dropping_the_googletest_library_is_caught(self) -> None:
        """Dropping it silently reverts to 11 unanalyzed test TUs."""
        text = _read(CMAKE_MODULE_PATH)
        mutated = re.sub(r"\n\s+--library=googletest", "", text, count=1)
        self.assertNotEqual(mutated, text)
        self.assertIn("--library=googletest", " ".join(cppcheck_gate_violations(mutated)))

    def test_reintroducing_the_redundant_suppress_is_caught(self) -> None:
        text = _read(CMAKE_MODULE_PATH)
        mutated = text.replace(
            "        --suppress=missingIncludeSystem",
            "        --suppress=missingInclude\n        --suppress=missingIncludeSystem",
            1,
        )
        self.assertNotEqual(mutated, text)
        violations = " ".join(cppcheck_gate_violations(mutated))
        self.assertIn("missingInclude", violations)

    def test_dropping_a_required_suppression_is_caught(self) -> None:
        """Dropping one would fail the gate on noise rather than on findings."""
        text = _read(CMAKE_MODULE_PATH)
        mutated = re.sub(r"\n\s+--suppress=unmatchedSuppression", "", text, count=1)
        self.assertNotEqual(mutated, text)
        self.assertIn("unmatchedSuppression", " ".join(cppcheck_gate_violations(mutated)))

    def test_adding_an_unexpected_suppression_is_caught(self) -> None:
        """A blanket suppression must not be able to creep in unnoticed."""
        text = _read(CMAKE_MODULE_PATH)
        mutated = text.replace(
            "        --suppress=unmatchedSuppression",
            "        --suppress=unmatchedSuppression\n        --suppress=useStlAlgorithm",
            1,
        )
        self.assertNotEqual(mutated, text)
        self.assertIn("useStlAlgorithm", " ".join(cppcheck_gate_violations(mutated)))

    def test_dropping_a_result_check_is_caught(self) -> None:
        text = _read(WORKFLOWS_DIR / STATIC_ANALYSIS_WORKFLOW)
        mutated = text.replace(
            "          R_CPPCHECK: ${{ needs.cppcheck.result }}\n",
            "",
        )
        self.assertNotEqual(mutated, text)
        violations = gate_violations(STATIC_ANALYSIS_WORKFLOW, mutated)
        self.assertTrue(any("needs.cppcheck.result" in violation for violation in violations))


if __name__ == "__main__":
    unittest.main(verbosity=2)
