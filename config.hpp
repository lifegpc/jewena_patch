#include <unordered_map>
#include <string>

class Config {
    public:
        std::unordered_map<std::string, std::string> configs;
        Config() {
            configs["defaultFont"] = "微软雅黑";
            configs["stringReplaceFile"] = "";
            configs["appendLogging"] = "false";
            configs["loggingFile"] = "";
            configs["loggingLevel"] = "info";
        }
        bool Load(std::string path);
        bool IsAppendLogging();
        int LoggingLevel();
};
