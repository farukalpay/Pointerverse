# Pointerverse Guard Demo

This fixture shows the M6 PR guard on a directory diff. `before/` is the base
tree and `after/` is the proposed PR tree.

```sh
pointerverse guard run \
  --repo examples/pr_guard/after \
  --base ../before \
  --mode observe \
  --out report.md \
  --format markdown
```

Expected signals include a changed auth source without a matching test update,
a possible secret in `config/dev.env`, a workflow change, a lockfile change,
generated output, and a deleted test.
