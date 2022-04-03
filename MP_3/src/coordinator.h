#include <string>
#include <algorithm>

enum ServerStatus { 
    ACTIVE, 
    INACTIVE 
};

enum ServerType {
    PRIMARY,
    SECONDARY
};

ServerType parse_type(std::string type) {
	std::transform(type.begin(), type.end(), type.begin(),
		[](unsigned char c) {
			return std::tolower(c);
		}
	);
	if (type == "master" || type == "primary")
		return ServerType::PRIMARY;

	return ServerType::SECONDARY;
}

struct ServerEntry {
    std::string sid;        // cluster SID
    std::string hostname;
    std::string port;
    enum ServerStatus status;
    enum ServerType type;
    
    bool operator==(const ServerEntry& e1) const {
        return sid == e1.sid;
    }
};