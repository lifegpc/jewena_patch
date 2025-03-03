#include <unordered_map>
#include <string>

class StringReplaceFile {
    public:
        std::unordered_map<std::string, std::string> messages;
        bool Load(std::string file);
};
