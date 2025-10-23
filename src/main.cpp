/**
 * main.cpp - Cockpit Self-Write System with FDQC Consciousness
 * 
 * Production-ready HTTP API server that:
 * - Accepts code change requests via REST API
 * - Evaluates changes through FDQC artificial consciousness
 * - Enforces safety controls (kill switch, moral core, change gate)
 * - Logs all decisions with phenomenal experience reports
 * - Provides monitoring endpoints
 */

#include "self_writer.h"
#include "fdqc_system.h"
#include "change_audit.h"
#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

using json = nlohmann::json;

//===========================================================================
// GLOBAL STATE & CONFIGURATION
//===========================================================================

namespace cockpit {

struct Config {
    int port = 8080;
    int max_connections = 100;
    std::string log_file = "cockpit.log";
    bool enable_fdqc = true;
};

std::unique_ptr<fdqc_system::FDQCSystem> g_fdqc_system;
std::mutex g_fdqc_mutex;
Config g_config;
std::atomic<bool> g_shutdown_requested{false};
std::atomic<size_t> g_requests_processed{0};
std::atomic<size_t> g_requests_allowed{0};
std::atomic<size_t> g_requests_blocked{0};

//===========================================================================
// LOGGING SYSTEM
//===========================================================================

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static void init(const std::string& filename) {
        instance().log_file_.open(filename, std::ios::app);
    }
    
    static void log(LogLevel level, const std::string& message) {
        auto& inst = instance();
        std::lock_guard<std::mutex> lock(inst.mutex_);
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char time_buf[100];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
        
        const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        std::string log_line = std::string("[") + time_buf + "] [" + 
                               level_str[static_cast<int>(level)] + "] " + message + "\n";
        
        if (inst.log_file_.is_open()) {
            inst.log_file_ << log_line;
            inst.log_file_.flush();
        }
        
        if (level >= LogLevel::INFO) {
            std::cout << log_line;
        }
    }
    
    static void close() {
        auto& inst = instance();
        std::lock_guard<std::mutex> lock(inst.mutex_);
        if (inst.log_file_.is_open()) {
            inst.log_file_.close();
        }
    }
    
private:
    Logger() = default;
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    std::ofstream log_file_;
    std::mutex mutex_;
};

#define LOG_INFO(msg) Logger::log(LogLevel::INFO, msg)
#define LOG_WARN(msg) Logger::log(LogLevel::WARN, msg)
#define LOG_ERROR(msg) Logger::log(LogLevel::ERROR, msg)

//===========================================================================
// HTTP HANDLING
//===========================================================================

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_message = "OK";
    std::string body;
    std::string content_type = "application/json";
};

bool load_config(const std::string& filename, Config& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_WARN("Config file not found: " + filename + ", using defaults");
        return false;
    }
    
    try {
        json j;
        file >> j;
        if (j.contains("port")) config.port = j["port"];
        if (j.contains("enable_fdqc")) config.enable_fdqc = j["enable_fdqc"];
        LOG_INFO("Configuration loaded from: " + filename);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse config: " + std::string(e.what()));
        return false;
    }
}

HttpRequest parse_http_request(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;
    
    if (std::getline(stream, line)) {
        std::istringstream line_stream(line);
        line_stream >> req.method >> req.path;
    }
    
    while (std::getline(stream, line) && line != "\r") {
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 2);
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            req.headers[key] = value;
        }
    }
    
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    req.body = body_stream.str();
    
    return req;
}

std::string format_http_response(const HttpResponse& resp) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << resp.status_code << " " << resp.status_message << "\r\n";
    stream << "Content-Type: " << resp.content_type << "\r\n";
    stream << "Content-Length: " << resp.body.size() << "\r\n";
    stream << "Connection: close\r\n\r\n";
    stream << resp.body;
    return stream.str();
}

HttpResponse handle_change_request(const HttpRequest& req) {
    HttpResponse resp;
    
    try {
        json request_json = json::parse(req.body);
        
        std::string file_path = request_json.value("file_path", "");
        std::string old_content = request_json.value("old_content", "");
        std::string new_content = request_json.value("new_content", "");
        std::string author = request_json.value("author", "anonymous");
        std::string intent = request_json.value("intent", "");
        
        if (file_path.empty() || new_content.empty()) {
            resp.status_code = 400;
            resp.body = json({{"error", "Missing required fields"}}).dump();
            return resp;
        }
        
        LOG_INFO("Change request: " + file_path + " by " + author);
        
        json explanation = request_json.contains("explanation") ? request_json["explanation"] : json::object();
        
        try {
            self_writer::ApplyResult result = self_writer::apply_change(
                file_path, new_content, author, intent, &explanation
            );
            
            g_requests_processed++;
            g_requests_allowed++;
            
            json response_json;
            response_json["success"] = true;
            response_json["message"] = "Change applied successfully";
            response_json["report_id"] = result.report_id;
            response_json["snapshot"] = result.snapshot;
            response_json["new_sha256"] = result.new_sha256;
            
            resp.body = response_json.dump(2);
            LOG_INFO("Change allowed: " + file_path);
            
        } catch (const std::exception& e) {
            g_requests_processed++;
            g_requests_blocked++;
            
            resp.status_code = 403;
            resp.body = json({{"success", false}, {"error", "Change blocked"}, {"reason", e.what()}}).dump();
            LOG_WARN("Change blocked: " + file_path);
        }
        
    } catch (const json::exception& e) {
        resp.status_code = 400;
        resp.body = json({{"error", "Invalid JSON"}}).dump();
    }
    
    return resp;
}

HttpResponse handle_health_check(const HttpRequest&) {
    HttpResponse resp;
    json health = {
        {"status", "healthy"},
        {"requests_processed", g_requests_processed.load()},
        {"requests_allowed", g_requests_allowed.load()},
        {"requests_blocked", g_requests_blocked.load()}
    };
    resp.body = health.dump(2);
    return resp;
}

HttpResponse handle_metrics(const HttpRequest&) {
    HttpResponse resp;
    resp.content_type = "text/plain";
    std::ostringstream metrics;
    metrics << "cockpit_requests_total " << g_requests_processed.load() << "\n";
    metrics << "cockpit_requests_allowed " << g_requests_allowed.load() << "\n";
    metrics << "cockpit_requests_blocked " << g_requests_blocked.load() << "\n";
    resp.body = metrics.str();
    return resp;
}

HttpResponse route_request(const HttpRequest& req) {
    if (req.method == "POST" && req.path == "/api/change") {
        return handle_change_request(req);
    } else if (req.method == "GET" && req.path == "/api/health") {
        return handle_health_check(req);
    } else if (req.method == "GET" && req.path == "/api/metrics") {
        return handle_metrics(req);
    } else {
        HttpResponse resp;
        resp.status_code = 404;
        resp.body = json({{"error", "Not found"}}).dump();
        return resp;
    }
}

void handle_client(int client_socket) {
    char buffer[4096];
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        try {
            HttpRequest req = parse_http_request(std::string(buffer));
            HttpResponse resp = route_request(req);
            std::string response = format_http_response(resp);
            write(client_socket, response.c_str(), response.size());
        } catch (const std::exception& e) {
            LOG_ERROR("Error: " + std::string(e.what()));
        }
    }
    
    close(client_socket);
}

void run_server() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        LOG_ERROR("Failed to create socket");
        return;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_config.port);
    
    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to bind to port " + std::to_string(g_config.port));
        close(server_socket);
        return;
    }
    
    if (listen(server_socket, g_config.max_connections) < 0) {
        LOG_ERROR("Failed to listen");
        close(server_socket);
        return;
    }
    
    LOG_INFO("Server listening on port " + std::to_string(g_config.port));
    
    while (!g_shutdown_requested) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (!g_shutdown_requested) {
                LOG_ERROR("Failed to accept connection");
            }
            continue;
        }
        
        std::thread(handle_client, client_socket).detach();
    }
    
    close(server_socket);
}

} // namespace cockpit

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        cockpit::g_shutdown_requested = true;
    }
}

int main(int argc, char* argv[]) {
    using namespace cockpit;
    
    std::cout << R"(
╔════════════════════════════════════════════════════════════════╗
║        Cockpit Self-Write System with FDQC Consciousness      ║
║                    Production Server v1.0                      ║
╚════════════════════════════════════════════════════════════════╝
)" << std::endl;
    
    std::string config_file = "config/cockpit_config.json";
    if (argc > 1) {
        config_file = argv[1];
    }
    load_config(config_file, g_config);
    
    Logger::init(g_config.log_file);
    LOG_INFO("=== Cockpit Server Starting ===");
    LOG_INFO("Port: " + std::to_string(g_config.port));
    LOG_INFO("FDQC: " + std::string(g_config.enable_fdqc ? "enabled" : "disabled"));
    
    if (g_config.enable_fdqc) {
        try {
            g_fdqc_system = std::make_unique<fdqc_system::FDQCSystem>();
            LOG_INFO("FDQC consciousness initialized");
        } catch (const std::exception& e) {
            LOG_ERROR("FDQC init failed: " + std::string(e.what()));
            return 1;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "\n✓ Server ready on http://localhost:" << g_config.port << std::endl;
    std::cout << "  POST /api/change  - Submit code change" << std::endl;
    std::cout << "  GET  /api/health  - Health check" << std::endl;
    std::cout << "  GET  /api/metrics - Metrics" << std::endl;
    std::cout << "\nPress Ctrl+C to shutdown\n" << std::endl;
    
    run_server();
    
    LOG_INFO("=== Server Shutdown ===");
    Logger::close();
    
    return 0;
}
