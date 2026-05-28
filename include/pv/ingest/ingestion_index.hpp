// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "pv/runtime/ids.hpp"

namespace pv {

class IngestionIndex {
public:
    explicit IngestionIndex(std::filesystem::path repository_root);

    [[nodiscard]] bool seen(std::string_view source, std::string_view event_id) const;
    void mark_seen(std::string source, std::string event_id, CommitId commit);

private:
    using Key = std::pair<std::string, std::string>;

    void load();
    [[nodiscard]] std::filesystem::path path() const;

    std::filesystem::path repository_root_;
    std::map<Key, CommitId> entries_;
};

}  // namespace pv
