#include <cstring>

bool login(const char* user) {
    return user != nullptr && std::strcmp(user, "admin") == 0;
}
