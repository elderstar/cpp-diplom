#pragma once

#include <string>
#include <map>
#include <regex>

class Spider {
public:
    Spider(const std::string& _start_url, int _max_depth, const std::string& db_host, const std::string& db_port, const std::string& db_name, const std::string& db_user, const std::string& db_password);
    void start();
    bool initDb();

private:
    std::string fetchPage(const std::string& url);
    std::string eraseTags(const std::string& html);
    std::string erasePuncts(const std::string& html);
    std::map<std::string, int> buildIndex(const std::string& text);
    void saveToDb(const std::string& url, const std::map<std::string, int>& index);
    std::smatch parseUrl(const std::string& url);
     
    std::string _start_url;
    int _max_depth;
    std::string _db_host;
    std::string _db_name;
    std::string _db_port;
    std::string _db_user;
    std::string _db_password;
};