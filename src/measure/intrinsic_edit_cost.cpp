// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/intrinsic_edit_cost.hpp"

#include <cctype>
#include <utility>

namespace pv {
namespace {

bool is_word_char(unsigned char ch) noexcept {
    return std::isalnum(ch) || ch == '_' || ch == '.' || ch == '/' || ch == '-';
}

}  // namespace

std::vector<std::string> canonical_repair_tokens(std::string_view script) {
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < script.size()) {
        const auto ch = static_cast<unsigned char>(script[index]);
        if (std::isspace(ch)) {
            index += 1;
            continue;
        }
        if (script[index] == '#') {
            while (index < script.size() && script[index] != '\n') {
                index += 1;
            }
            continue;
        }
        if (script[index] == '-' && index + 1 < script.size() && script[index + 1] == '>') {
            tokens.emplace_back("->");
            index += 2;
            continue;
        }
        if (script[index] == ':' || script[index] == '=') {
            tokens.emplace_back(1, script[index]);
            index += 1;
            continue;
        }

        std::string token;
        while (index < script.size()) {
            const auto current = static_cast<unsigned char>(script[index]);
            if (!is_word_char(current)) {
                break;
            }
            if (script[index] == '-' && index + 1 < script.size() && script[index + 1] == '>') {
                break;
            }
            token.push_back(script[index]);
            index += 1;
        }
        if (!token.empty()) {
            tokens.push_back(std::move(token));
            continue;
        }

        tokens.emplace_back(1, script[index]);
        index += 1;
    }
    return tokens;
}

std::string canonical_repair_script(std::string_view script) {
    const auto tokens = canonical_repair_tokens(script);
    std::string out;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index != 0) {
            out.push_back(' ');
        }
        out += tokens[index];
    }
    return out;
}

std::uint64_t canonical_edit_cost(std::string_view script) {
    return static_cast<std::uint64_t>(canonical_repair_script(script).size());
}

CanonicalEditCost IntrinsicEditCost::measure(std::string_view script) const {
    auto canonical = canonical_repair_script(script);
    const auto tokens = canonical_repair_tokens(script);
    return CanonicalEditCost{
        static_cast<std::uint64_t>(canonical.size()),
        static_cast<std::uint64_t>(tokens.size()),
        std::move(canonical)
    };
}

}  // namespace pv
