#include "Seeker.h"
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

    int searcher_port = std::stoi(config["seeker.port"]);
    std::string db_host = config["db.host"];
    std::string db_name = config["db.name"];
    std::string db_port = config["db.port"];
    std::string db_user = config["db.user"];
    std::string db_password = config["db.password"];

    Seeker seeker(searcher_port, db_host, db_port, db_name, db_user, db_password);
    seeker.start();

    return 0;
}