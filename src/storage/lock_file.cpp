// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/lock_file.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#if !defined(_WIN32)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pv {

RepositoryWriteLock::RepositoryWriteLock(std::filesystem::path root)
    : path_(std::move(root) / "locks" / "write.lock") {
    std::filesystem::create_directories(path_.parent_path());
#if defined(_WIN32)
    if (!std::filesystem::create_directory(path_)) {
        throw std::runtime_error("repository write lock is already held");
    }
    owns_ = true;
#else
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd_ < 0) {
        if (errno == EEXIST) {
            throw std::runtime_error("repository write lock is already held");
        }
        throw std::runtime_error(std::string{"cannot create repository write lock: "} + std::strerror(errno));
    }
    const auto text = std::string{"locked\n"};
    (void)::write(fd_, text.data(), text.size());
    owns_ = true;
#endif
}

RepositoryWriteLock::RepositoryWriteLock(RepositoryWriteLock&& other) noexcept
    : path_(std::move(other.path_)), owns_(other.owns_)
#if !defined(_WIN32)
      , fd_(other.fd_)
#endif
{
    other.owns_ = false;
#if !defined(_WIN32)
    other.fd_ = -1;
#endif
}

RepositoryWriteLock& RepositoryWriteLock::operator=(RepositoryWriteLock&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        owns_ = other.owns_;
#if !defined(_WIN32)
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.owns_ = false;
    }
    return *this;
}

RepositoryWriteLock::~RepositoryWriteLock() {
    release();
}

const std::filesystem::path& RepositoryWriteLock::path() const noexcept {
    return path_;
}

void RepositoryWriteLock::release() noexcept {
    if (!owns_) {
        return;
    }
#if !defined(_WIN32)
    if (fd_ >= 0) {
        (void)::close(fd_);
        fd_ = -1;
    }
#endif
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    owns_ = false;
}

}  // namespace pv
