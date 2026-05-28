// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pv {

struct RelationType {
    std::uint32_t id{0};

    [[nodiscard]] bool valid() const noexcept { return id != 0; }

    friend bool operator==(RelationType, RelationType) = default;
};

enum class CausalRole : std::uint8_t {
    Structural,
    Observational,
    Generative,
    Inhibitory,
    Transformative,
    Ancestral,
    Symbolic
};

class RelationRegistry {
public:
    [[nodiscard]] RelationType intern(std::string_view name);
    [[nodiscard]] std::optional<RelationType> find(std::string_view name) const;
    [[nodiscard]] const std::string& name(RelationType type) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<std::string>& names() const noexcept;
    void restore_names(const std::map<std::uint32_t, std::string>& names);

private:
    std::unordered_map<std::string, RelationType> ids_;
    std::vector<std::string> names_;
};

[[nodiscard]] std::string to_string(RelationType relation);
[[nodiscard]] std::string to_string(CausalRole role);
[[nodiscard]] CausalRole causal_role_from_string(std::string_view value);

}  // namespace pv
