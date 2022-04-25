#include <string>
#include <queue>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <chrono>

#include "sns.grpc.pb.h"
using csce438::FlaggedDataEntry;

enum ServerStatus { 
    ACTIVE, 
    INACTIVE 
};

enum ServerType {
    PRIMARY,
    SECONDARY,
    NONE
};

ServerType parse_type(std::string type) {
    
	std::transform(type.begin(), type.end(), type.begin(),
		[](unsigned char c) {
			return std::tolower(c);
		}
	);
    std::cout << "Input server type string=" << type << '\n';//(!)
	if (type == "master" || type == "primary") {
		return ServerType::PRIMARY;
    } else if (type == "slave" || type == "secondary") {
    	return ServerType::SECONDARY;
    } else {
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
            return std::string("NONE");
    }
}
std::vector<std::string> split_string(std::string s, std::string delim=",") {
    // Split a string on delim
    std::vector<std::string> parts;
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delim)) != std::string::npos) {
        token = s.substr(0, pos);
        parts.push_back(token);
        s.erase(0, pos + delim.length());
    }
    parts.push_back(s);
    return parts;
}
struct ServerEntry {
    std::string sid; //cluster ID
    std::string hostname;
    std::string primary_port;
    std::string secondary_port;
    std::string sync_port;

    std::vector<std::string> clients_served;

    enum ServerStatus primary_status = ServerStatus::INACTIVE;
    enum ServerStatus secondary_status = ServerStatus::INACTIVE;

    std::chrono::system_clock::time_point primary_last;
    std::chrono::system_clock::time_point secondary_last;

    ServerEntry(std::string s, std::string hname, std::string server_port, ServerType t): sid(s), hostname(hname) { 
        update_entry(server_port, t);
    }
    void update_entry(std::string port, ServerType t) {
        // Default to primary being active
        if (t == ServerType::PRIMARY) {

            if (secondary_status == ServerStatus::ACTIVE) {
                std::cout <<
                    "------- WARNING -------\n"
                    "Primary is coming online after secondary was already promoted.\n"
                    "This is currently unsupported and may cause undefined behavior\n"
                    "such as missed messages served by secondary. This can be rectified\n"
                    "by adding datastore copy methods which pull updates from secondary's\n"
                    "file system...\n"
                    "-----------------------\n";
            }

            primary_port = port;
            primary_status = ServerStatus::ACTIVE;
            primary_last = std::chrono::high_resolution_clock::now();
        } else {
            secondary_port = port;
            secondary_last = std::chrono::high_resolution_clock::now();
        }
    }
    void set_active(ServerType t) {
        if (t == ServerType::PRIMARY) {
            primary_status = ServerStatus::ACTIVE;
        } else {
            secondary_status = ServerStatus::ACTIVE;
        }
    }
    bool operator==(const ServerEntry& e1) const {
        return sid == e1.sid;
    }
    void promote_secondary() {
        // * Mark prim as inactive and secondary as active
        primary_status = ServerStatus::INACTIVE;
        secondary_status = ServerStatus::ACTIVE;
    }
    void heartbeat_timestamp(const std::string& type_str) {
        if (type_str == "primary") {
            primary_last = std::chrono::high_resolution_clock::now();
        } else {
            secondary_last = std::chrono::high_resolution_clock::now();
        }
    }
};

struct ClientEntry {
    std::string cid;        // client ID
    std::string sid;        // assigned clusterID
    std::vector<std::string> followers;
    std::queue<std::string> forwards;

    ClientEntry(std::string client_id, std::string server_id) : cid(client_id), sid(server_id) { 
        followers.push_back(cid);
    }
    bool has_forwards() const {
        return !forwards.empty();
    }
    std::string pop_next_forward() {
        std::string fwd = forwards.front();
        forwards.pop();
        return fwd;
    }
};
