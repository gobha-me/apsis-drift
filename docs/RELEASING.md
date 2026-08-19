# Release Checklist

## Every release

- Build and test the publication commit with both GCC and Clang.
- Run the headless benchmark smoke path without presenting renderer throughput
  as terminal, proxy, or display throughput.
- Confirm the application version, tag, release title, and documented save
  policy agree.
- Merge only after pull-request CI is green; verify the resulting `main` CI
  before publishing the tag and GitHub release.

## Save compatibility

- Before `v0.8.0`, state explicitly in the release notes that saves are alpha
  and may be incompatible. Name any format reset and confirm rejected source
  files remain untouched.
- For `v0.8.0`, record the actual current save format as the minimum supported
  beta format. Do not publish it as beta until skippable onboarding, the
  open-exploration handoff, and ordinary save/load UI are suitable for ongoing
  careers.
- After `v0.8.0`, verify every supported beta-line fixture under GCC and Clang.
- For 1.x, retain the supported v0.8+ lineage and all valid 1.x saves. Breaking
  persistence or generator changes require migration or a new major-version
  policy.
