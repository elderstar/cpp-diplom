#include "Seeker.h"
#include <pqxx/pqxx>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/beast/version.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

Seeker::Seeker(int port, const std::string& _db_host, const std::string& db_port, const std::string& _db_name, const std::string& _db_user, const std::string& _db_password)
    : port(port), _db_host(_db_host), _db_port(db_port), _db_name(_db_name), _db_user(_db_user), _db_password(_db_password) {}

std::vector<std::string> Seeker::split(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;

    while (getline(ss, item, delim)) { 
        result.push_back(item);
    }

    return result;
}
void Seeker::start() {
    net::io_context ioc;
    tcp::acceptor acceptor{ ioc, {tcp::v4(), static_cast<unsigned short>(port)} };

    for (;;) {
        tcp::socket socket{ ioc };
        acceptor.accept(socket);

        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);

        http::response<http::string_body> res{ http::status::ok, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");

        if (req.method() == http::verb::get) {
            res.body() = "<html><body><form action='/search' method='post'><input type='text' name='query'/><input type='submit' value='Search'/></form></body></html>";
        }
        else if (req.method() == http::verb::post) {
            auto query = req.body();
            std::cout << query << std::endl;

            size_t pos = query.find('=');

            if (pos == std::string::npos)
            {
                res.result(http::status::not_found);
                res.set(http::field::content_type, "text/plain");
                res.body() = "Not found\r\n";
                return;
            }

            std::string key = query.substr(0, pos);
            query = query.substr(pos + 1);

            if (key != "query")
            {
                res.result(http::status::not_found);
                res.set(http::field::content_type, "text/plain");
                res.body() = "Not found\r\n";
                return;
            }

            std::vector<std::string> words = split(query, '+');

            if (words.empty()) {
                res.body() = "No results found\r\n";
                return;
            }

            auto result = searchDb(words);
            res.body() = "<html><body><h1>Search Results</h1><pre>" + result + "</pre></body></html>";
        }
        else {
            res.result(http::status::bad_request);
            res.body() = "Invalid request method";
        }

        res.prepare_payload();
        http::write(socket, res);
    }
}

    std::string Seeker::searchDb(const std::vector<std::string>& words) {
    std::string result;

    try {
        pqxx::connection C("host=" + _db_host + " port=" + _db_port + " dbname=" + _db_name + " user=" + _db_user + " password=" + _db_password);
        pqxx::work W(C);

        std::string in_clause;
        for (const auto& word : words) {
            in_clause += W.quote(word) + ",";
        }
        in_clause.pop_back();
       
        std::cout << in_clause << std::endl;

        std::string sql = "SELECT d.url, SUM(dw.frequency) as relevance \
            FROM documents d \
            JOIN words_to_documents dw ON d.id = dw.document_id \
            JOIN words w ON dw.word_id = w.id \
            WHERE w.word IN(" + in_clause + ") \
            GROUP BY d.url \
            ORDER BY relevance DESC \
            LIMIT 10; ";
        pqxx::result R = W.exec(sql);
        
        for (const auto& row : R) {
            result += "<li><a href=\"";
            result += row["url"].c_str();
            result += "\">";
            result += row["url"].c_str();
            result += "</a> (Relevance: ";
            result += row["relevance"].c_str();
            result += ")</li>";
        }
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return result;
}
