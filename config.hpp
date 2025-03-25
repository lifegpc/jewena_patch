#include <unordered_map>
#include <string>
#include <list>
#include <utility>
#include "patch_config.h"

class Config {
    public:
        std::unordered_map<std::string, std::string> configs;
#if HAVE_MPV
        std::list<std::pair<std::string, std::string>> mpv;
#endif
        Config() {
            configs["defaultFont"] = "微软雅黑";
            configs["stringReplaceFile"] = "";
            #if HAVE_PLAYER
            configs["appendLogging"] = "false";
            configs["loggingFile"] = "";
            configs["loggingLevel"] = "info";
            configs["audioBuffer"] = "0";
            configs["videoBuffer"] = "0";
            #endif
        }
        bool Load(std::string path);
#if HAVE_PLAYER
        bool IsAppendLogging();
        int LoggingLevel();
        uint32_t AudioBuffer();
        uint32_t VideoBuffer();
#endif
};
