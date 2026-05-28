# Pointerverse Guard

- risk score: **100 / 100**
- status: **critical**
- changed files: **6**

## Findings

- **high** `modified_source_requires_test`: src/auth.cpp modified but no matching test file changed
- **critical** `secret_pattern_in_diff_is_critical`: possible secret introduced in config/dev.env
- **high** `workflow_change_is_high_risk`: .github/workflows/deploy.yml changes CI or deployment workflow state
- **medium** `lockfile_change_requires_policy`: package-lock.json changed without policy approval
- **medium** `generated_file_change_is_medium_risk`: src/generated/client.cpp appears to be generated or vendored output
- **high** `deleted_test_is_high_risk`: tests/auth_test.cpp deleted from test coverage
- **info** `changed_files_mapped_into_audit_graph`: 6 changed files mapped into audit graph
