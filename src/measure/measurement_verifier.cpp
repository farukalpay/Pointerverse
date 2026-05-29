// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/measurement_verifier.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include "pv/hash/canonical.hpp"
#include "pv/measure/component_record.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/measure/risk_evidence.hpp"
#include "pv/measure/risk_functional.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

void add_error(MeasurementVerificationReport& report, std::string message) {
    report.errors.push_back(MeasurementVerificationError{std::move(message)});
}

std::string short_hash(Hash256 hash) {
    return to_hex(hash).substr(0, 12);
}

}  // namespace

MeasurementVerifier::MeasurementVerifier(Repository& repository) : repository_(repository) {}

MeasurementVerificationReport MeasurementVerifier::verify_branch(
    std::string_view branch,
    const MeasurementSpec& spec,
    const Verifier* verifier,
    MeasurementVerificationOptions options) const {
    MeasurementVerificationReport report;
    report.branch = std::string{branch};
    report.spec_hash = measurement_spec_hash(spec);

    if (spec.verifier_id != "none" && verifier == nullptr) {
        add_error(report, "measurement spec requires verifier '" + spec.verifier_id + "'");
        return report;
    }

    MeasurementStore store{repository_};
    try {
        const auto stored_spec = store.load_spec(report.spec_hash);
        if (!(stored_spec == spec)) {
            add_error(report, "stored measurement spec body does not match requested spec");
        }
    } catch (const std::exception& error) {
        add_error(report, std::string{"measurement spec cannot be loaded: "} + error.what());
    }

    for (const auto& entry : store.index().branch_entries(branch, report.spec_hash)) {
        report.measurements_checked += 1;
        if (entry.needs_rebuild) {
            const auto message = "measurement cache entry needs rebuild: " + short_hash(entry.measurement_object);
            if (options.strict_cache) {
                add_error(report, message);
            } else {
                report.warnings.push_back(message);
            }
            continue;
        }
        try {
            const auto record = store.load_record(entry.measurement_object);
            if (record.commit != entry.commit || record.spec_hash != report.spec_hash) {
                add_error(report, "measurement index row does not match record: " + short_hash(entry.measurement_object));
                continue;
            }
            if (record.legacy) {
                const auto message = "legacy measurement record needs rebuild: " + short_hash(entry.measurement_object);
                if (options.strict_cache) {
                    add_error(report, message);
                } else {
                    report.warnings.push_back(message);
                }
                continue;
            }
            if (record.measurement_identity_hash != entry.measurement_identity_hash) {
                add_error(report, "measurement identity differs from index: " + short_hash(entry.measurement_object));
            }
            if (record.component_root != measurement_component_root(record.component_objects)) {
                add_error(report, "measurement component root mismatch: " + short_hash(entry.measurement_object));
            }
            if (record.evidence_root != measurement_evidence_root(record.evidence_objects)) {
                add_error(report, "measurement evidence root mismatch: " + short_hash(entry.measurement_object));
            }
            for (const auto component_object : record.component_objects) {
                const auto component = decode_measurement_component_record_bytes(repository_.objects().get_bytes(component_object));
                if (measurement_component_hash(component) != component_object) {
                    add_error(report, "measurement component object hash mismatch: " + short_hash(component_object));
                }
            }
            for (const auto evidence_object : record.evidence_objects) {
                const auto evidence = decode_risk_evidence_bytes(repository_.objects().get_bytes(evidence_object));
                if (risk_evidence_hash(evidence) != evidence_object) {
                    add_error(report, "measurement evidence object hash mismatch: " + short_hash(evidence_object));
                }
            }

            const auto recomputed = MeasuredRiskFunctional{}.measure_commit(repository_, branch, entry.commit, spec, verifier);
            if (record.commit_root != recomputed.commit_root) {
                add_error(report, "measurement commit root mismatch: " + short_hash(entry.measurement_object));
            }
            if (entry.risk != recomputed.value) {
                add_error(report, "measurement index risk differs from recomputation: " + short_hash(entry.measurement_object));
            }
            if (record.component_root != recomputed.component_root) {
                add_error(report, "measurement component root differs from recomputation: " + short_hash(entry.measurement_object));
            }
            if (record.evidence_root != recomputed.evidence_root) {
                add_error(report, "measurement evidence root differs from recomputation: " + short_hash(entry.measurement_object));
            }
            if (record.measurement_identity_hash != recomputed.measurement_hash) {
                add_error(report, "measurement hash differs from recomputation: " + short_hash(entry.measurement_object));
            }
        } catch (const std::exception& error) {
            add_error(report, fmt::format(
                "measurement verification failed for {}: {}",
                short_hash(entry.measurement_object),
                error.what()));
        }
    }

    return report;
}

std::string render_measurement_verification_text(
    const MeasurementVerificationReport& report,
    const MeasurementSpec& spec) {
    std::ostringstream output;
    output << "Measurement verification\n";
    output << "------------------------\n";
    output << fmt::format("branch: {}\n", report.branch);
    output << fmt::format("measurements checked: {}\n", report.measurements_checked);
    output << fmt::format("spec: {}:v{}\n", spec.id, spec.version);
    output << fmt::format("spec hash: {}\n", short_hash(report.spec_hash));
    output << fmt::format("status: {}\n", report.clean() ? "clean" : "dirty");
    if (!report.errors.empty()) {
        output << "\nerrors:\n";
        for (const auto& error : report.errors) {
            output << "  " << error.message << '\n';
        }
    }
    if (!report.warnings.empty()) {
        output << "\nwarnings:\n";
        for (const auto& warning : report.warnings) {
            output << "  " << warning << '\n';
        }
    }
    return output.str();
}

}  // namespace pv
