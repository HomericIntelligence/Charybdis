# Security Policy

## Reporting Security Vulnerabilities

**Do not open public issues for security vulnerabilities.**

We take security seriously. If you discover a security vulnerability, please report it responsibly.

## How to Report

### Email (Preferred)

Send an email to: **<security@homericintelligence.com>**

Or use the GitHub private vulnerability reporting feature if available.

### What to Include

Please include as much of the following information as possible:

- **Description** - Clear description of the vulnerability
- **Impact** - Potential impact and severity assessment
- **Steps to reproduce** - Detailed steps to reproduce the issue
- **Affected files** - Which source files or configurations are affected
- **Suggested fix** - If you have a suggested fix or mitigation

### Example Report

```text
Subject: [SECURITY] Fault injection test escapes sandbox and targets production subjects

Description:
The chaos test harness does not validate NATS subject prefixes, allowing
a fault injection test to accidentally (or intentionally) target production
subjects outside the test namespace.

Impact:
A misconfigured test could disrupt production agent coordination by
injecting faults into live NATS subjects.

Steps to Reproduce:
1. Write a chaos test targeting subject "hi.myrmidon.worker.>"
2. Run just test
3. Observe faults published to production subjects

Affected Files:
src/fault_injector.cpp (subject validation)

Suggested Fix:
Validate that all fault injection targets use the "hi.test." subject prefix.
```

## Response Timeline

We aim to respond to security reports within the following timeframes:

| Stage                    | Timeframe              |
|--------------------------|------------------------|
| Initial acknowledgment   | 48 hours               |
| Preliminary assessment   | 1 week                 |
| Fix development          | Varies by severity     |
| Public disclosure        | After fix is released  |

## Severity Assessment

We use the following severity levels:

| Severity     | Description                          | Response           |
|--------------|--------------------------------------|--------------------|
| **Critical** | Remote code execution, data breach   | Immediate priority |
| **High**     | Privilege escalation, data exposure  | High priority      |
| **Medium**   | Limited impact vulnerabilities       | Standard priority  |
| **Low**      | Minor issues, hardening              | Scheduled fix      |

## Responsible Disclosure

We follow responsible disclosure practices:

1. **Report privately** - Do not disclose publicly until a fix is available
2. **Allow reasonable time** - Give us time to investigate and develop a fix
3. **Coordinate disclosure** - We will work with you on disclosure timing
4. **Credit** - We will credit you in the security advisory (if desired)

## What We Will Do

When you report a vulnerability:

1. Acknowledge receipt within 48 hours
2. Investigate and validate the report
3. Develop and test a fix
4. Release the fix
5. Publish a security advisory

## Scope

### In Scope

- C++ chaos testing source code and fault injection logic
- CMake build configuration
- Dockerfile and container configuration
- Test harness and fixture configurations

### Out of Scope

- ProjectAgamemnon `/v1/chaos/*` API (report to [ProjectAgamemnon](https://github.com/HomericIntelligence/ProjectAgamemnon))
- Other HomericIntelligence submodule repos (report to that repo directly)
- Third-party dependencies (report upstream)
- Social engineering attacks
- Physical security

## MemorySanitizer (MSan) Status

MSan is intentionally absent from CI. Uninstrumented system libraries (libc++, libstdc++,
glibc) produce unavoidable false positives because MSan requires **every** library in the
process — including the C++ standard library — to be compiled with `-fsanitize=memory`.

To enable MSan locally you must build a fully instrumented copy of libc++ from LLVM source
and link against it. A skeleton CMake preset (`msan`, marked `hidden: true`) is recorded in
`CMakePresets.json` to document what would be needed. Until a fully instrumented toolchain is
available in CI, prefer ASAN (`--preset debug`) and TSan (`--preset tsan`) for dynamic
analysis.

## Security Best Practices

When contributing to ProjectCharybdis:

- Isolate fault injection to test environments and namespaced NATS subjects
- Validate all injection targets before publishing
- Use AddressSanitizer (ASAN) and ThreadSanitizer (TSAN) in CI builds
- Never commit secrets, API keys, tokens, or credentials
- Pin FetchContent dependency versions to known-good commits

## Contact

For security-related questions that are not vulnerability reports:

- Open a GitHub Discussion with the "security" tag
- Email: <security@homericintelligence.com>

---

Thank you for helping keep HomericIntelligence secure!
