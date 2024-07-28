#include "Spider.h"
#include <pqxx/pqxx>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/beast/version.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/url.hpp>
#include <openssl/ssl.h> 

#include <thread>
#include <queue>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace algo = boost::algorithm;
using tcp = net::ip::tcp;

Spider::Spider(const std::string& start_url, int max_depth, const std::string& db_host, const std::string& db_port, const std::string& db_name, const std::string& db_user, const std::string& db_password)
    : _start_url(start_url), _max_depth(max_depth), _db_host(db_host), _db_port(db_port), _db_name(db_name), _db_user(db_user), _db_password(db_password) {}

void Spider::start() {
    std::queue<std::pair<std::string, int>> url_queue;
    std::unordered_set<std::string> visited_urls;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    bool done = false;
    
    boost::urls::url_view url(_start_url);
    std::string host = url.host();

    auto worker = [&]() {
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [&]() { return !url_queue.empty() || done; });

            if (done && url_queue.empty()) {
                return;
            }

            auto [url, depth] = url_queue.front();
            url_queue.pop();

            lock.unlock();

            if (depth > _max_depth) {
                continue;
            }

            std::string html = fetchPage(url);

            std::string text = eraseTags(html);
            text = erasePuncts(text);
            
            auto index = buildIndex(text);
            saveToDb(url, index);

            std::regex re("<a href=\"(.*?)\"");
            std::smatch match;
            std::string::const_iterator search_start(html.cbegin());
            while (std::regex_search(search_start, html.cend(), match, re)) 
            {
                if (match[1] != "" || match[1] != "/" || match[1] != "#")
                {
                    std::string new_url = buildUrl(match[1], host);
                    if (visited_urls.find(new_url) == visited_urls.end()) {
                        visited_urls.insert(new_url);
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex);
                            url_queue.emplace(new_url, depth + 1);
                        }
                        queue_cv.notify_one();
                    }
                    search_start = match.suffix().first;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker);
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        url_queue.emplace(_start_url, 1);
        visited_urls.insert(_start_url);
    }
    queue_cv.notify_one();

    for (auto& thread : threads) {
        thread.join();
    }
}

std::string Spider::fetchPage(const std::string& url) {
    try
    {
        std::cout << std::this_thread::get_id() << " try to fetch: " + url << std::endl;

        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        std::smatch parsed_url = parseUrl(url);
        std::string host = parsed_url[4].str();
        std::string schema = parsed_url[2].str();

        if (schema == "https")
        {
            ssl::context ctx(ssl::context::tlsv13_client);
            ctx.set_default_verify_paths();
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
            stream.set_verify_mode(ssl::verify_none);

            stream.set_verify_callback([](bool pre_verified, ssl::verify_context& ctx) {
                return true; 
            });

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
                throw beast::system_error{ ec };
            }

            tcp::resolver resolver(ioc);
            get_lowest_layer(stream).connect(resolver.resolve({ host, schema }));
            get_lowest_layer(stream).expires_after(std::chrono::seconds(15));

            http::request<http::empty_body> req{ http::verb::get, url, 11 };
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            stream.handshake(ssl::stream_base::client);
            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(stream, buffer, res);
            //if(res.result_int() === 200);

            beast::error_code ec;
            stream.shutdown(ec);

            return beast::buffers_to_string(res.body().data());
        }
        else if(schema == "http") 
        {
            auto const results = resolver.resolve(host, schema);
            stream.connect(results);

            http::request<http::empty_body> req{ http::verb::get, url, 11 };
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(stream, buffer, res);
            //if(res.result_int() === 200);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            return beast::buffers_to_string(res.body().data());
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return "0";
    }
}

std::string Spider::eraseTags(const std::string& html) {

    std::regex re("<[^>]*>");
    return std::regex_replace(html, re, "");
}

std::string Spider::erasePuncts(const std::string& html) {

    std::regex punc_regex("[[:punct:]]");
    return std::regex_replace(html, punc_regex, "");
}

std::string Spider::lowercase(const std::string& str) {

    return algo::to_lower_copy(str);
}

std::string Spider::buildUrl(const std::string& url, const std::string& host)
{
    if (url.find("://") == std::string::npos)
    {
        std:std::string div = "";
        if (url.at(0) != '/')
            div = '/';

        return "https://" + host + div + url;
    }
    else {
        return url;
    }
}

std::map<std::string, int> Spider::buildIndex(const std::string& text) {
    std::map<std::string, int> index;
    std::istringstream iss(text);
    std::string word;

    while (iss >> word) {
        if (word.size() > 3 && word.size() < 33)
        {
            word = lowercase(word);
            index[word]++;
        }
    }
    return index;
}

std::smatch Spider::parseUrl(const std::string& url)
{
    //std::regex ex("(http|https)://([^/ :]+):?([^/ ]*)(/?[^ #?]*)\\x3f?([^ #]*)#?([^ ]*)");
    int counter = 0;
    std::vector<std::string> result;

    std::regex url_regex(
        R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)",
        std::regex::extended
    );
    std::smatch url_match_result;

    if (!std::regex_match(url, url_match_result, url_regex)) 
    {
        std::cout << "Wrong URL" << std::endl;
    }
    return url_match_result;
}

bool Spider::initDb() {
    try {
        pqxx::connection C("host=" + _db_host + " port=" + _db_port + " dbname=" + _db_name + " user=" + _db_user + " password=" + _db_password);
        pqxx::work W(C);

        W.exec("CREATE TABLE IF NOT EXISTS documents (id SERIAL NOT NULL, url VARCHAR(255) NOT NULL UNIQUE, PRIMARY KEY (id));");

        W.exec("CREATE TABLE IF NOT EXISTS words (id SERIAL NOT NULL, word VARCHAR(255) NOT NULL UNIQUE, PRIMARY KEY (id))");

        W.exec("CREATE TABLE IF NOT EXISTS words_to_documents ("
            "word_id int NOT NULL, "
            "document_id int NOT NULL, "
            "frequency int NOT NULL, "
            "FOREIGN KEY(word_id) REFERENCES words(id), "
            "FOREIGN KEY(document_id) REFERENCES documents(id))");

        W.commit();
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}

void Spider::saveToDb(const std::string& url, const std::map<std::string, int>& index) {
    try {
        pqxx::connection C("host=" + _db_host + " port=" + _db_port + " dbname=" + _db_name + " user=" + _db_user + " password=" + _db_password);
        pqxx::work W(C); 
        std::cout << std::this_thread::get_id() << " save result of: " + url << std::endl;
        pqxx::result R = W.exec("INSERT INTO documents (url) VALUES (" + W.quote(url) + ") ON CONFLICT (url) DO NOTHING RETURNING id;");

        int doc_id = 0;

        if (!R.empty()) {
            doc_id = R[0][0].as<int>();
        }
        else {
            pqxx::result select_result = W.exec("SELECT id FROM documents WHERE url = " + W.quote(url) + ";");

            if (!select_result.empty()) {
                doc_id = select_result[0][0].as<int>();
            }
            else {
                throw std::runtime_error("Failed to retrieve ID for existing document.");
            }
        }

        for (const auto& [word, freq] : index) {

            R = W.exec("INSERT INTO words (word) VALUES (" + W.quote(word) + ") ON CONFLICT (word) DO NOTHING RETURNING id;");

            int word_id = 0;

            if (!R.empty()) {
                word_id = R[0][0].as<int>();
            }
            else {
                pqxx::result select_result = W.exec("SELECT id FROM words WHERE word = " + W.quote(word) + ";");

                if (!select_result.empty()) {
                    word_id = select_result[0][0].as<int>();
                }
                else {
                    throw std::runtime_error("Failed to retrieve ID for existing word.");
                }
            }

            if (word_id != 0 && doc_id != 0)
            {
                W.exec("INSERT INTO words_to_documents (document_id, word_id, frequency) VALUES (" 
                    + W.quote(doc_id) + ", " + W.quote(word_id) + ", " + W.quote(freq) + ");");
            }
        }
        W.commit();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}
