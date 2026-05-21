#ifndef TRANSLATE_UTILS_SYSTEM_H
#define TRANSLATE_UTILS_SYSTEM_H

#include <stdexcept>
#include <string>

namespace translate::utils {
/*
  Exit codes follow driver/returncodes.py.
*/
enum class ExitCode {
    SUCCESS = 0,
    TRANSLATE_UNSOLVABLE = 10,
    TRANSLATE_OUT_OF_MEMORY = 20,
    TRANSLATE_OUT_OF_TIME = 21,
    TRANSLATE_CRITICAL_ERROR = 30,
    TRANSLATE_INPUT_ERROR = 31,
    TRANSLATE_UNSUPPORTED = 35,
};

class ExitException : public std::exception {
    ExitCode exit_code;
    std::string message;
public:
    ExitException(ExitCode code, std::string msg)
        : exit_code(code), message(std::move(msg)) {}
    ExitCode get_exit_code() const { return exit_code; }
    const char *what() const noexcept override { return message.c_str(); }
};

[[noreturn]] void exit_with(ExitCode code);
[[noreturn]] void exit_with(ExitCode code, const std::string &message);
}

#endif
