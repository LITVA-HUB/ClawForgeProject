# Security Policy

NexaClaw is a local-first AI gateway and control plane. Security-sensitive areas include auth token mode, rate limiting, scoped tools, audit logs, browser relay behavior, Telegram pairing, task execution, and model-provider configuration.

## Supported scope

Security reports are welcome for:

- authentication or authorization bypasses;
- unsafe tool execution;
- rate-limit bypasses;
- audit-log integrity issues;
- browser relay boundary issues;
- secret handling problems;
- unsafe default configuration;
- denial-of-service risks in local gateway flows.

## Reporting

Please do not open a public GitHub issue for security vulnerabilities.

Use GitHub private vulnerability reporting if available. If it is not available, open a minimal public issue that says only:

> Security report available. Please enable private vulnerability reporting or provide a private contact.

Do not include exploit details, secrets, payloads, or private logs in public issues.

## Maintainer response

The maintainer will triage reports, confirm impact where possible, prepare a fix, and publish notes once the issue is resolved.
