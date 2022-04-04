#include <string>
#include <algorithm>

enum ServerStatus { 
    ACTIVE, 
    INACTIVE 
};

enum ServerType {
    PRIMARY,
    SECONDARY,
    INVALID
};

ServerType parse_type(std::string type) {
    
	std::transform(type.begin(), type.end(), type.begin(),
		[](unsigned char c) {
			return std::tolower(c);
		}
	);
    std::cout << "Input server type string=" << type << '\n';//(!)
	if (type == "master" || type == "primary") {
        std::cout << "Returning ServerType::PRIMARY\n";//(!)
		return ServerType::PRIMARY;
    } else if (type == "slave" || type == "secondary") {
        std::cout << "Returning ServerType::SECONDARY\n";//(!)
    	return ServerType::SECONDARY;
    } else {
        std::cout << "Returning ServerType::INVALID\n";//(!)
        // return ServerType::INVALID;
        return (ServerType)2;
    }
}

std::string type_to_string(ServerType t) {
    switch (t) {
        case ServerType::PRIMARY:
            return std::string("PRIMARY");
        case ServerType::SECONDARY:
            return std::string("SECONDARY");
        default:
            return std::string("INVALID");
    }
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

struct SyncServiceEntry {
    std::string PLACEHOLDER;
    std::string sid;
};