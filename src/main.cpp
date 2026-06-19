#include "StorageEngine.h"
#include "ThreadPool.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unordered_map>
#include <shared_mutex>

using namespace std;

#define PORT 9000
#define BUFFER_SIZE 1024

/**
 * ============================================================================
 * DATABASE MANAGER (COLLECTIONS)
 * ============================================================================
 * Manages multiple StorageEngine instances dynamically. 
 * Allows users to have "users", "products", "orders" as separate .db files.
 */
class DatabaseManager {
private:
    std::unordered_map<std::string, StorageEngine*> collections;
    std::shared_mutex map_lock;

public:
    StorageEngine* get_or_create_collection(const std::string& name) {
        // 1. First, acquire a read lock to see if it already exists (Fast Path)
        {
            std::shared_lock<std::shared_mutex> lock(map_lock);
            auto it = collections.find(name);
            if (it != collections.end()) {
                return it->second;
            }
        }
        
        // 2. If not found, acquire a write lock to create it (Slow Path)
        std::unique_lock<std::shared_mutex> lock(map_lock);
        
        // Double-check in case another thread created it while we were waiting for the write lock
        auto it = collections.find(name);
        if (it != collections.end()) {
            return it->second;
        }
        
        // 3. Create the new physical database file
        std::string filename = name + ".db";
        StorageEngine* new_db = storage_engine_create(filename.c_str());
        if (new_db) {
            collections[name] = new_db;
            cout << "  [SYSTEM] Auto-created new collection file: " << filename << "\n";
        }
        return new_db;
    }
    
    ~DatabaseManager() {
        std::unique_lock<std::shared_mutex> lock(map_lock);
        for (auto& pair : collections) {
            storage_engine_destroy(pair.second);
        }
    }
};

/**
 * ============================================================================
 * DATABASE TCP SERVER
 * ============================================================================
 */
void handle_client(int client_socket, DatabaseManager* db_manager) {
    char buffer[BUFFER_SIZE];
    
    string welcome = "=== Welcome to CustomDB Server ===\nType HELP for commands.\n";
    send(client_socket, welcome.c_str(), welcome.length(), 0);
    
    while (true) {
        string prompt = "db> ";
        send(client_socket, prompt.c_str(), prompt.length(), 0);
        
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_read <= 0) break;
        
        string line(buffer);
        size_t last_valid = line.find_last_not_of(" \n\r\t");
        if (last_valid != string::npos) line.erase(last_valid + 1);
        else line.clear();
        
        if (line.empty()) continue;
        
        stringstream ss(line);
        string command;
        ss >> command;
        for (auto& c : command) c = toupper(c);
        
        string response = "";
        
        if (command == "EXIT" || command == "QUIT") {
            response = "Closing connection. Goodbye!\n";
            send(client_socket, response.c_str(), response.length(), 0);
            break;
        }
        else if (command == "HELP") {
            response = "Commands:\n"
                       "  INSERT <collection> <key> <value>\n"
                       "  SEARCH <collection> <key>\n"
                       "  STATS <collection>\n"
                       "  EXIT\n";
        }
        else if (command == "STATS") {
            string coll;
            ss >> coll;
            if (coll.empty()) {
                response = "[ERROR] Usage: STATS <collection>\n";
            } else {
                StorageEngine* db = db_manager->get_or_create_collection(coll);
                if (db) {
                    storage_engine_print_stats(db);
                    response = "Stats for '" + coll + "' printed to the main server console.\n";
                } else {
                    response = "[ERROR] Could not load collection.\n";
                }
            }
        }
        else if (command == "SEARCH") {
            string coll, key;
            ss >> coll >> key;
            if (key.empty()) {
                response = "[ERROR] Usage: SEARCH <collection> <key>\n";
            } else {
                StorageEngine* db = db_manager->get_or_create_collection(coll);
                if (!db) {
                    response = "[ERROR] Could not load collection.\n";
                } else {
                    char value_buffer[MAX_VALUE_SIZE];
                    if (storage_engine_search(db, key.c_str(), value_buffer)) {
                        response = "[FOUND in " + coll + "] " + key + " -> " + string(value_buffer) + "\n";
                    } else {
                        response = "[NOT FOUND] Key '" + key + "' does not exist in '" + coll + "'.\n";
                    }
                }
            }
        }
        else if (command == "INSERT") {
            string coll, key;
            ss >> coll >> key;
            
            string value;
            getline(ss, value);
            size_t start = value.find_first_not_of(" \t");
            if (start != string::npos) value = value.substr(start);
            else value = "";
            
            if (key.empty() || value.empty()) {
                response = "[ERROR] Usage: INSERT <collection> <key> <value>\n";
            } else {
                StorageEngine* db = db_manager->get_or_create_collection(coll);
                if (!db) {
                    response = "[ERROR] Could not load collection.\n";
                } else {
                    if (storage_engine_insert(db, key.c_str(), value.c_str())) {
                        response = "[SUCCESS] Inserted key: " + key + " into '" + coll + "'\n";
                    } else {
                        response = "[FAILURE] Could not insert key.\n";
                    }
                }
            }
        }
        else {
            response = "[ERROR] Unknown command: " + command + "\n";
        }
        
        if (!response.empty()) {
            send(client_socket, response.c_str(), response.length(), 0);
        }
    }
    
    close(client_socket);
    cout << "Client disconnected.\n";
}

int main() {
    cout << "=== Custom Database Engine TCP Server ===\n";
    cout << "Initializing Database Manager...\n";
    
    DatabaseManager db_manager;
    
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    ThreadPool pool(num_threads);
    
    cout << "Thread Pool running with " << num_threads << " workers.\n";
    
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        return 1;
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return 1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    cout << "\n============================================\n";
    cout << "SERVER ONLINE & LISTENING ON PORT " << PORT << "\n";
    cout << "To connect, open a new terminal and type:\n";
    cout << "  telnet localhost " << PORT << "\n";
    cout << "============================================\n\n";
    
    while (true) {
        int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        cout << "New client connected! Handing off to Thread Pool...\n";
        
        pool.enqueue([client_socket, &db_manager]() {
            handle_client(client_socket, &db_manager);
        });
    }
    
    return 0;
}
