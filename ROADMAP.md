# NexaClaw Roadmap

This roadmap tracks work that makes NexaClaw more useful as a self-hosted AI gateway and local-first control plane.

## Near term

- Stabilize CI for Linux and macOS.
- Expand C++ tests for gateway auth, rate limiting, scoped tools, and audit logs.
- Improve CLI diagnostics and `doctor` output.
- Document safe self-hosting patterns.
- Add release checklist and versioned release notes.

## Security hardening

- Add regression tests for token auth.
- Add rate-limit bypass tests.
- Document browser relay security boundaries.
- Review secret handling and provider configuration.
- Improve audit-log coverage for sensitive actions.

## Developer experience

- Improve quickstart examples.
- Add minimal example configs.
- Add more troubleshooting docs.
- Improve OpenClaw migration notes.
- Add issue templates for bugs, features, and security-sensitive reports.

## Longer term

- Harden browser relay primitives.
- Expand task orchestration support.
- Improve model routing and provider diagnostics.
- Add more reproducible install and release workflows.
