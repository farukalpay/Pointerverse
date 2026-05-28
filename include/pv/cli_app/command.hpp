// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CLI/CLI.hpp>

namespace pv::cli_app {

class Command {
public:
    virtual ~Command() = default;

    virtual void register_with(CLI::App& app) = 0;
    virtual int run() = 0;

    [[nodiscard]] virtual bool selected() const noexcept { return root_ != nullptr && root_->parsed(); }

protected:
    void set_root(CLI::App* root) noexcept { root_ = root; }
    [[nodiscard]] CLI::App* root() const noexcept { return root_; }

private:
    CLI::App* root_{nullptr};
};

}  // namespace pv::cli_app
