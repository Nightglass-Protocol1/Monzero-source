<!-- Copy of core/SECURITY.md so GitHub shows it as this repository's security policy; keep the two identical. -->
# Monzero security policy

## Reporting a vulnerability

Report suspected vulnerabilities privately to
[`security@monzero.org`](mailto:security@monzero.org). Do not open a public
issue until the report has been assessed and a coordinated disclosure date has
been agreed.

Include the affected commit or release, operating system, impact, reproduction
steps, and the smallest safe proof of concept needed to demonstrate the issue.
Do not send wallet seeds, private keys, passwords, personal data, or live funds.
Email is not end-to-end encrypted until a dedicated security OpenPGP key is
published.

The project aims to acknowledge a report within seven calendar days, provide a
status update within fourteen days, and coordinate disclosure after a fix is
available. A 90-day disclosure window is the default target, but urgent active
exploitation or complex consensus changes may require a different schedule
agreed with the reporter. Good-faith reports will be credited unless the
reporter asks to remain anonymous.

## Public security research program

Monzero welcomes good-faith security research. This is currently a
coordinated vulnerability-disclosure program, not a promise of payment.
Reward eligibility and amount must be confirmed by the project in writing;
until a funded reward schedule is published, researchers should assume that
no monetary reward is guaranteed.

Use a private test network for disruptive, high-volume, consensus-fork, or
denial-of-service testing. Testing the public network is permitted only when it
uses accounts and systems owned by the researcher, does not degrade service,
does not attempt to reorganize the public chain, and does not access or alter
another person's data or funds. Stop immediately if testing causes unexpected
service degradation or affects another user.

The following activities are not authorized:

- denial of service, resource exhaustion, traffic flooding, or destructive
  stress testing against public infrastructure;
- theft, attempted theft, double spending, inflation, or intentional public
  chain reorganization, even if funds would later be returned;
- privacy attacks, deanonymization, phishing, social engineering, credential
  collection, persistence, or accessing data that is not the researcher's;
- testing hosting providers, upstream projects, exchanges, pools, or other
  third parties without their separate permission; and
- public disclosure before the coordinated disclosure date.

If sensitive data is encountered accidentally, stop, do not retain or share
it, and report only the minimum information necessary to locate the exposure.
Do not demand payment or threaten disclosure.

The project will not initiate legal action against research conducted in good
faith and in accordance with this policy. This statement cannot authorize
activity on third-party systems or bind third parties. Researchers remain
responsible for complying with applicable law.

Reports are evaluated primarily by demonstrated impact and reproducibility.
Highest priority is given to unauthorized coin creation, consensus splits,
remote code execution, wallet key or seed exposure, signature or proof
bypass, theft of funds, and remotely exploitable denial of service. A scanner
result without reproducible security impact, self-XSS, a missing header with
no exploit, and issues affecting only obsolete or modified builds are normally
informational.

## Scope

Reports concerning Monzero consensus, networking, wallets, release packages,
the website, explorer, and official deployment configuration are in scope.
Upstream Monero services, third-party exchanges, mining pools, hosting
providers, and user-operated infrastructure are outside the project's control;
report those issues to their respective operators.

Monzero v1.0.0 is the current stable release. It has not been independently
audited; security review continues with public use, and reports under this
policy are part of that review. This program does not replace an independent
security audit.
