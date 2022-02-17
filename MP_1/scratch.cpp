#include <iostream>

using namespace std;

// int main() {
//     // char str[] = "JOIN ROOMNAME bokehbox";
//     // string s = string(str);
//     string s = "JOIN ROOMNAME bokehbox";
//     string delim = " ";

//     cout << "START: " << s << endl;

//     size_t pos = 0;
//     string tok;
//     while ((pos = s.find(delim)) != string::npos) {
//         tok = s.substr(0, pos);
//         cout << tok << endl;
//         s.erase(0, pos + delim.length());
//     }
//     cout << tok << endl;

//     return 0;
// }

int main() {

    string s = "JOIN ROOMNAME bokehbox";
    string delimiter = " ";

    size_t last = 0;
    size_t next = 0;
    while ((next = s.find(delimiter, last)) != string::npos) {
        cout << s.substr(last, next-last) << endl;
        last = next + 1;
    }
    cout << s.substr(last) << endl;
}
