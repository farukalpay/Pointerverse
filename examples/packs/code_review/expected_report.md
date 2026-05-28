## Pointerverse Guard

Risk score: **100 / 100**
Status: **critical**
Changed files: **6**
Diff: **+24 -7**

### Findings

- **HIGH**: .github/workflows/deploy.yml changes CI or deployment workflow state (`.github/workflows/deploy.yml`) [`workflow_change_is_high_risk`]
- **CRITICAL**: possible secret introduced in config/dev.env (`config/dev.env:1`) [`secret_pattern_in_diff_is_critical`]
- **MEDIUM**: package-lock.json changed without policy approval (`package-lock.json`) [`lockfile_change_requires_policy`]
- **HIGH**: src/auth.cpp modified but no matching test file changed (`src/auth.cpp`) [`modified_source_requires_test`]
- **MEDIUM**: src/generated/client.cpp appears to be generated or vendored output (`src/generated/client.cpp`) [`generated_file_change_is_medium_risk`]
- **HIGH**: tests/auth_test.cpp deleted from test coverage (`tests/auth_test.cpp`) [`deleted_test_is_high_risk`]
- **INFO**: 6 changed files mapped into audit graph [`changed_files_mapped_into_audit_graph`]

### Artifacts

- `audit-report.md`
- `audit-report.json`
- `audit.sarif`
- `.pvstore/` replayable audit graph
