// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

namespace pv {

class RepositoryWriteLock {
public:
    explicit RepositoryWriteLock(std::filesystem::path root);
    RepositoryWriteLock(const RepositoryWriteLock&) = delete;
    RepositoryWriteLock& operator=(const RepositoryWriteLock&) = delete;
    RepositoryWriteLock(RepositoryWriteLock&& other) noexcept;
    RepositoryWriteLock& operator=(RepositoryWriteLock&& other) noexcept;
    ~RepositoryWriteLock();

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    void release() noexcept;

    std::filesystem::path path_;
    bool owns_{false};
#if !defined(_WIN32)
    int fd_{-1};
#endif
};

}  // namespace pv
