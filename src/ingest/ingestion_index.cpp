// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/ingestion_index.hpp"

#include <fstream>
#include <stdexcept>

#include "pv/hash/canonical.hpp"
#include "pv/ingest/evidence.hpp"

namespace pv {

IngestionIndex::IngestionIndex(std::filesystem::path repository_root)
    : repository_root_(std::move(repository_root)) {
    load();
}

bool IngestionIndex::seen(std::string_view source, std::string_view event_id) const {
    return entries_.contains(Key{std::string{source}, std::string{event_id}});
}

void IngestionIndex::mark_seen(std::string source, std::string event_id, CommitId commit) {
    if (!valid_evidence_key(source) || !valid_evidence_key(event_id)) {
        throw std::invalid_argument("evidence source and event_id cannot be empty or contain tabs/newlines");
    }

    const auto key = Key{source, event_id};
    if (entries_.contains(key)) {
        return;
    }

    const auto index_path = path();
    std::filesystem::create_directories(index_path.parent_path());
    std::ofstream output(index_path, std::ios::app);
    if (!output) {
        throw std::runtime_error("cannot write evidence ingestion index");
    }
    output << source << '\t' << event_id << '\t' << to_hex(commit.value) << '\n';
    output.close();
    if (!output) {
        throw std::runtime_error("failed writing evidence ingestion index");
    }
    entries_.emplace(key, commit);
}

void IngestionIndex::load() {
    entries_.clear();
    std::ifstream input(path());
    if (!input) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            throw std::runtime_error("invalid evidence ingestion index line");
        }
        auto commit = parse_hash256(line.substr(second + 1));
        if (!commit.has_value()) {
            throw std::runtime_error("invalid evidence ingestion index commit id");
        }
        entries_.emplace(
            Key{line.substr(0, first), line.substr(first + 1, second - first - 1)},
            CommitId{*commit});
    }
}

std::filesystem::path IngestionIndex::path() const {
    return repository_root_ / "index" / "evidence";
}

}  // namespace pv
