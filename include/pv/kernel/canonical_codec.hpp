// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/fact.hpp"
#include "pv/core/snapshot_page.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/kernel/program.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/trace/event.hpp"

namespace pv {

class CanonicalWriter {
public:
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void i64(std::int64_t value);
    void f64(double value);
    void string(std::string_view value);
    void hash(Hash256 value);
    void bytes(std::span<const std::byte> value);
    void sized_bytes(std::span<const std::byte> value);

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::vector<std::byte> take_bytes();

private:
    std::vector<std::byte> bytes_;
};

class CanonicalReader {
public:
    explicit CanonicalReader(std::span<const std::byte> bytes);

    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();
    [[nodiscard]] std::int64_t i64();
    [[nodiscard]] double f64();
    [[nodiscard]] std::string string();
    [[nodiscard]] Hash256 hash();
    [[nodiscard]] std::vector<std::byte> sized_bytes();

    void expect_end() const;
    void expect_tag(std::string_view tag);
    [[nodiscard]] bool eof() const noexcept;

private:
    [[nodiscard]] std::span<const std::byte> read(std::size_t count);

    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};

void encode(CanonicalWriter& writer, const WorldSnapshot& snapshot);
void encode(CanonicalWriter& writer, const Delta& delta);
void encode(CanonicalWriter& writer, const Value& value);
void encode(CanonicalWriter& writer, const Attribute& attribute);
void encode(CanonicalWriter& writer, const Fact& fact);
void encode(CanonicalWriter& writer, const TraceEvent& event);
void encode(CanonicalWriter& writer, const std::vector<TraceEvent>& events);
void encode(CanonicalWriter& writer, const Instruction& instruction);
void encode(CanonicalWriter& writer, const ProgramSymbolTable& symbols);
void encode(CanonicalWriter& writer, const Program& program);
void encode(CanonicalWriter& writer, const LawStatus& status);
void encode(CanonicalWriter& writer, const std::vector<LawStatus>& statuses);
void encode(CanonicalWriter& writer, const LawViolation& violation);
void encode(CanonicalWriter& writer, const std::vector<LawViolation>& violations);
void encode(CanonicalWriter& writer, const ObjectPage& page);
void encode(CanonicalWriter& writer, const PointerPage& page);
void encode(CanonicalWriter& writer, const FactPage& page);
void encode(CanonicalWriter& writer, const SymbolTableObject& table);
void encode(CanonicalWriter& writer, const SnapshotPageIndexObject& index);
void encode(CanonicalWriter& writer, const SnapshotRootObject& root);
void encode_morphism_path(CanonicalWriter& writer, const std::vector<std::string>& path);
void encode_commit_identity(CanonicalWriter& writer, const CommitRecord& record);
void encode_commit_record_body(CanonicalWriter& writer, const CommitRecord& record);
void encode_commit_record_body_v4(CanonicalWriter& writer, const CommitRecord& record);
void encode_commit_proof(CanonicalWriter& writer, const CommitProof& proof);

[[nodiscard]] WorldSnapshot decode_world_snapshot(CanonicalReader& reader);
[[nodiscard]] Delta decode_delta(CanonicalReader& reader);
[[nodiscard]] Value decode_value(CanonicalReader& reader);
[[nodiscard]] Attribute decode_attribute(CanonicalReader& reader);
[[nodiscard]] Fact decode_fact(CanonicalReader& reader);
[[nodiscard]] TraceEvent decode_trace_event(CanonicalReader& reader);
[[nodiscard]] std::vector<TraceEvent> decode_trace_events(CanonicalReader& reader);
[[nodiscard]] Instruction decode_instruction(CanonicalReader& reader);
[[nodiscard]] ProgramSymbolTable decode_program_symbol_table(CanonicalReader& reader);
[[nodiscard]] Program decode_program(CanonicalReader& reader);
[[nodiscard]] LawStatus decode_law_status(CanonicalReader& reader);
[[nodiscard]] std::vector<LawStatus> decode_law_statuses(CanonicalReader& reader);
[[nodiscard]] LawViolation decode_law_violation(CanonicalReader& reader);
[[nodiscard]] std::vector<LawViolation> decode_law_violations(CanonicalReader& reader);
[[nodiscard]] ObjectPage decode_object_page(CanonicalReader& reader);
[[nodiscard]] PointerPage decode_pointer_page(CanonicalReader& reader);
[[nodiscard]] FactPage decode_fact_page(CanonicalReader& reader);
[[nodiscard]] SymbolTableObject decode_symbol_table_object(CanonicalReader& reader);
[[nodiscard]] SnapshotPageIndexObject decode_snapshot_page_index_object(CanonicalReader& reader);
[[nodiscard]] SnapshotRootObject decode_snapshot_root_object(CanonicalReader& reader);
[[nodiscard]] std::vector<std::string> decode_morphism_path(CanonicalReader& reader);
[[nodiscard]] CommitRecord decode_commit_record_body(CanonicalReader& reader);
[[nodiscard]] CommitRecord decode_commit_record_body_v4(CanonicalReader& reader);
[[nodiscard]] CommitRecord decode_commit_record_body_v1(CanonicalReader& reader);
[[nodiscard]] CommitRecord decode_commit_record_body_v2(CanonicalReader& reader);
[[nodiscard]] CommitRecord decode_commit_record_body_legacy_proof(CanonicalReader& reader);
[[nodiscard]] CommitRecord decode_commit_record_body_v4_legacy_proof(CanonicalReader& reader);
[[nodiscard]] CommitProof decode_commit_proof(CanonicalReader& reader);
[[nodiscard]] CommitProof decode_commit_proof_v1(CanonicalReader& reader);

template <class T>
[[nodiscard]] std::vector<std::byte> canonical_encode(const T& value) {
    CanonicalWriter writer;
    encode(writer, value);
    return writer.take_bytes();
}

[[nodiscard]] std::vector<std::byte> canonical_encode_morphism_path(const std::vector<std::string>& path);
[[nodiscard]] std::vector<std::byte> canonical_encode_commit_identity(const CommitRecord& record);
[[nodiscard]] std::vector<std::byte> canonical_encode_commit_identity_v3(const CommitRecord& record);

}  // namespace pv
