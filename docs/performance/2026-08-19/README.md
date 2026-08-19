# Pilot Thermal Reentry Acceptance Evidence

These reports record the deterministic v0.4.17 Pilot thermal-reentry scenario
on 2026-08-19. Both builds used CMake 3.28.3 in Release mode and the repository's
pinned TermForge dependency.

| Build | Compiler | Report SHA-256 |
| --- | --- | --- |
| GCC | GCC 14.2.0 | `63fd04181f272bcb6615ef23921708d134780a9aba7bf5ba15657c73a4cb47ab` |
| Clang | Clang 20.1.8 | `63fd04181f272bcb6615ef23921708d134780a9aba7bf5ba15657c73a4cb47ab` |

The GCC and Clang JSON files are byte-identical. They preserve authoritative
Planetfall and thermal values separately from terminal/proxy throughput. The
companion three-frame headless smoke runs produced simulation checksum
`10031144440779418700`; measured renderer timing is intentionally not checked
in because it is host-load evidence rather than a gameplay acceptance value.

Reproduce either report with:

```bash
./build/apsis-drift --intersystem-planetfall-acceptance \
  --driver ansi --profile remote --report report.json
sha256sum report.json
```
