#include "config.hpp"
#include "fileop.h"
#include "file_reader.h"
#include "malloc.h"

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
