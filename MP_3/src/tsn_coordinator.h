#include <string>
#include <queue>
#include <algorithm>
#include <vector>
#include <unordered_set>

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
        // std::cout << "Returning ServerType::PRIMARY\n";//(!)
		return ServerType::PRIMARY;
    } else if (type == "slave" || type == "secondary") {
        // std::cout << "Returning ServerType::SECONDARY\n";//(!)
    	return ServerType::SECONDARY;
    } else {
        // std::cout << "Returning ServerType::NONE\n";//(!)
        // this messes up linter somehow
        // return ServerType::NONE;
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

// a tsn_database.h helper, could be refactored to reduce superfluous includes
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
// std::unordered_set<std::string> parse_stream_init_msg(std::string init_entry, std::string& sid) { // --(!) DEPRECATED?
//     // Set the cluster_id/sid for this SyncService, return a set of all clients
//     // which are being followed on this cluster for outbound msg forwards

// 	// * Parse message on delim=','
// 	std::vector<std::string> parts = split_string(init_entry, ",");

// 	// * Extract the SID and set sid by reference
// 	sid = parts[0];

// 	// * return the unordered set of followees for this cluster
// 	std::unordered_set<std::string> followees;
// 	for (int i = 1; i < parts.size(); ++i) {
// 		followees.insert(parts[i]);
// 	}
// 	return followees;
// }

// (!) add an entry for clients served when we know we can keep a vector of elements
struct ServerEntry {
    std::string sid; //cluster ID
    std::string hostname;

    // Using unordered set for faster lookups of forwarding
    // std::vector<std::string> clients_served;
    std::unordered_set<std::string> clients_served;
    // These are entries which contain a user followed by someone in this cluster
    std::queue<FlaggedDataEntry> forward_queue;

    std::string primary_port;
    std::string secondary_port;
    std::string sync_port;

    enum ServerStatus primary_status;
    enum ServerStatus secondary_status;

    ServerEntry(std::string s, std::string hname, std::string serve_port, ServerType t): sid(s), hostname(hname) { 
        if (t == ServerType::PRIMARY) {
            primary_port = serve_port;
            primary_status = ServerStatus::ACTIVE;
            secondary_status = ServerStatus::INACTIVE;
        } else {
            secondary_port = serve_port;
            secondary_port = ServerStatus::ACTIVE;
            primary_status = ServerStatus::INACTIVE;
        }
    }
    void update_entry(std::string port, ServerType type) {
        // Add the entry corresponding to the type, set status to active
        if (type == ServerType::PRIMARY) {
            primary_port = port;
            primary_status = ServerStatus::ACTIVE;
        } else {
            secondary_port = port;
            secondary_status = ServerStatus::ACTIVE;
        }
    }
    bool operator==(const ServerEntry& e1) const {
        return sid == e1.sid;
    }
    bool is_serving_user_from_vec(const std::vector<std::string>& v) const {
        for (const std::string& s: v) {
            if (clients_served.find(s) != clients_served.end()) {
                return true;
            }
        }
        return false;
    }
    // bool is_serving(const std::string& user) {
    //     return (std::find(clients_served.begin(), clients_served.end(), user) != clients_served.end());
    // }
    // bool is_serving_user(const std::string& user) {
    //     // return (clients_served.)
    //     return (clients_served.find(user) != clients_served.end());
    // }
};

// For client routing table - this could be consolidated w/ server routing table, but this
// makes CID->SID mappings easier to lookup
struct ClientEntry {
    std::string cid;        // client ID
    std::string sid;        // assigned clusterID
    std::vector<std::string> followers;

    ClientEntry(std::string client, std::string server) : cid(client), sid(server) { }
    // bool is_followed_by(std::string user) {
    //     return (std::find(followers.begin(), followers.end(), user) != followers.end());
    // }
};

// Forward routing table --- may be refactored later
// struct ForwardEntry {
//     std::string cid;
//     std::queue<FlaggedDataEntry> data_entries;
// };

