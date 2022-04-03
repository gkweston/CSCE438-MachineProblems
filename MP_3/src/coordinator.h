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
    std::cout << "RPC server type=" << type << '\n';//(!)
	std::transform(type.begin(), type.end(), type.begin(),
		[](unsigned char c) {
			return std::tolower(c);
		}
	);
    std::cout << "RPC server type=" << type << '\n';//(!)
	if (type == "master" || type == "primary")
		return ServerType::PRIMARY;
    else if (type == "slave" || type == "secondary")
    	return ServerType::SECONDARY;
    else
        return ServerType::INVALID;
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