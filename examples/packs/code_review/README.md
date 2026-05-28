# Pointerverse Guard Demo

This fixture shows the PR guard on a directory diff. `before/` is the base
tree and `after/` is the proposed PR tree.

```sh
pointerverse guard run \
  --repo examples/packs/code_review/after \
  --base ../before \
  --mode observe \
  --markdown-out audit-report.md \
  --json-out audit-report.json \
  --sarif-out audit.sarif
```

Expected signals include a changed auth source without a matching test update,
a possible secret in `config/dev.env`, a workflow change, a lockfile change,
generated output, and a deleted test. In GitHub Actions these same findings can
be shown as a sticky PR comment, Check annotations, and Code Scanning SARIF.
