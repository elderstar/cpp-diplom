#include "Spider.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <map>
#include <string>
#include <filesystem>
#include <iostream>

void readConfig(const std::string& filename, std::map<std::string, std::string>& config) {
    
    if (!std::filesystem::exists(filename))
    {
        std::cout << "Can't find config file!" << std::endl;
    }
    else {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(filename, pt);
        for (const auto& section : pt) {
            for (const auto& key : section.second) {
                config[section.first + "." + key.first] = key.second.data();
            }
        }
    }
}

int main() {
    system("chcp 1251");
    setlocale(LC_ALL, "ru_RU.UTF-8");

    std::map<std::string, std::string> config;
    readConfig("../config.ini", config);

    std::string start_url = config["spider.start_url"];
    std::string max_depth_str = config["spider.depth"];
    int max_depth = 0;

    std::string db_host = config["db.host"];
    std::string db_name = config["db.name"];
    std::string db_port = config["db.port"];
    std::string db_user = config["db.user"];
    std::string db_password = config["db.password"];

    if (!max_depth_str.empty())
    {
        max_depth = std::stoi(max_depth_str);
    }

    if (max_depth <= 0)
    {
        std::cout << "Depth has to be greater than 0. Exit." << std::endl;
        exit(0);
    }

    if (start_url.empty())
    {
        std::cout << "URL can not be empty. Exit." << std::endl;
        exit(0);
    }

    Spider spider(start_url, max_depth, db_host, db_port, db_name, db_user, db_password);
    if (!spider.initDb())
    {
        std::cout << "DB initialization failed. Exit." << std::endl;
        exit(0);
    }
    spider.start();

    return 0;
}