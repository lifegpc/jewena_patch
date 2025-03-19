#include "config.hpp"
#include "fileop.h"
#include "file_reader.h"
#include "malloc.h"
#include "str_util.h"
#include "player.h"

#include <fcntl.h>
#if _WIN32
#include <Windows.h>
#endif

bool Config::Load(std::string path) {
    if (!fileop::exists(path)) {
        return false;
    }
    int fd = 0;
    int err = fileop::open(path, fd, O_RDONLY, _SH_DENYRW, _S_IREAD);
    if (err < 0) {
        return false;
    }
    FILE* f = fileop::fdopen(fd, "rb");
    if (!f) {
        fileop::close(fd);
        return false;
    }
    auto reader = create_file_reader(f, 0);
    char* line = nullptr;
    size_t line_size = 0;
    while (!file_reader_read_line(reader, &line, &line_size)) {
        std::string l(line, line_size);
        free(line);
        line = nullptr;
        line_size = 0;
        size_t comment_pos = l.find_first_of('#');
        if (comment_pos != std::string::npos) {
            l = l.substr(0, comment_pos);
        }
        size_t eq_pos = l.find_first_of('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        std::string key = l.substr(0, eq_pos);
        std::string value = l.substr(eq_pos + 1);
        configs[key] = value;
    }
    free_file_reader(reader);
    fileop::fclose(f);
    return true;
}

bool Config::IsAppendLogging() {
    auto re = configs.find("appendLogging");
    if (re == configs.end()) {
        return false;
    }
    return str_util::parse_bool((*re).second);  
}

int Config::LoggingLevel() {
    auto re = configs.find("loggingLevel");
    if (re == configs.end()) {
        return AV_LOG_INFO;
    }
    std::string level = str_util::tolower((*re).second);
    if (level == "debug") {
        return AV_LOG_DEBUG;
    } else if (level == "verbose") {
        return AV_LOG_VERBOSE;
    } else if (level == "info") {
        return AV_LOG_INFO;
    } else if (level == "warning") {
        return AV_LOG_WARNING;
    } else if (level == "error") {
        return AV_LOG_ERROR;
    } else if (level == "fatal") {
        return AV_LOG_FATAL;
    } else if (level == "trace") {
        return AV_LOG_TRACE;
    } else if (level == "panic") {
        return AV_LOG_PANIC;
    } else {
        return AV_LOG_INFO;
    }
}
