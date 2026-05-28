// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "pv/guard/policy_pack.hpp"
#include "pv/guard/report.hpp"

using namespace pv;

namespace {

GitDiffEntry entry(GitDiffStatus status, std::string path, int additions = 0, int deletions = 0) {
    GitDiffEntry out;
    out.status = status;
    out.path = std::move(path);
    out.additions = additions;
    out.deletions = deletions;
    return out;
}

bool has_rule(const std::vector<GuardFinding>& findings, std::string_view rule) {
    for (const auto& finding : findings) {
        if (finding.rule == rule) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("PR guard policy pack emits concrete PR risk findings") {
    std::vector<GitDiffEntry> entries;
    entries.push_back(entry(GitDiffStatus::Modified, "src/payment.cpp", 20, 4));
    entries.push_back(entry(GitDiffStatus::Added, "config/dev.env", 1, 0));
    entries.back().added_lines.push_back(GitDiffLine{"config/dev.env", 1, "SECRET_KEY=demo"});
    entries.push_back(entry(GitDiffStatus::Modified, "package-lock.json", 2, 2));
    entries.push_back(entry(GitDiffStatus::Modified, ".github/workflows/deploy.yml", 8, 1));
    entries.push_back(entry(GitDiffStatus::Modified, "src/generated/client.cpp", 10, 1));
    entries.back().added_lines.push_back(GitDiffLine{"src/generated/client.cpp", 3, "// @generated"});
    entries.push_back(entry(GitDiffStatus::Deleted, "tests/auth_test.cpp", 0, 30));
    entries.push_back(entry(GitDiffStatus::Modified, "docs/large.md", 500, 1));

    const auto findings = PrGuardPolicyPack{}.evaluate(entries);

    REQUIRE(has_rule(findings, "modified_source_requires_test"));
    REQUIRE(has_rule(findings, "secret_pattern_in_diff_is_critical"));
    REQUIRE(has_rule(findings, "lockfile_change_requires_policy"));
    REQUIRE(has_rule(findings, "workflow_change_is_high_risk"));
    REQUIRE(has_rule(findings, "generated_file_change_is_medium_risk"));
    REQUIRE(has_rule(findings, "large_diff_is_medium_risk"));
    REQUIRE(has_rule(findings, "deleted_test_is_high_risk"));
    REQUIRE(has_rule(findings, "changed_files_mapped_into_audit_graph"));
    REQUIRE(guard_risk_score(findings) == 100);
    REQUIRE(guard_status_for_risk(guard_risk_score(findings)) == "critical");
}

TEST_CASE("matching test change satisfies modified source policy") {
    std::vector<GitDiffEntry> entries;
    entries.push_back(entry(GitDiffStatus::Modified, "src/payment.cpp", 10, 1));
    entries.push_back(entry(GitDiffStatus::Modified, "tests/payment_test.cpp", 5, 1));

    const auto findings = PrGuardPolicyPack{}.evaluate(entries);

    REQUIRE_FALSE(has_rule(findings, "modified_source_requires_test"));
}
