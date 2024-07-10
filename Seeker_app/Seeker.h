#pragma once

#include <string>
#include <vector>

class Seeker {
public:
    Seeker(int port, const std::string& _db_host, const std::string& db_port, const std::string& _db_name, const std::string& _db_user, const std::string& _db_password);
    void start();

private:
    std::string searchDb(const std::vector<std::string>& words);
    std::vector<std::string> split(const std::string& str, char delim);
    int port;
    std::string _db_host;
    std::string _db_name;
    std::string _db_port;
    std::string _db_user;
    std::string _db_password;
};
