#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <map>
#include <string>
#include <mutex>
#include <atomic>
#include <ctime>

// Header Windows
#include <winsock2.h>
#include <windows.h>

#include <sqlite3.h> // Database SQLite3

#include <nlohmann/json.hpp> // JSON parsing e serialization
using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "sqlite3.lib")

// Strutture dati
struct SecurityEvent {
    uint32_t timestamp;
    float confidence;
    bool is_pir;
    bool is_vibration;
    uint32_t event_id;
    std::string location;
}; // Traspoto dei dati dai sensori al sistema host

struct AlarmEvent {
    uint32_t timestamp;
    float final_confidence;
    int trigger_type;
    std::string location;
    std::string threat_level;
}; // Allarmi processati dall'host

// Comunicazione seriale
class SerialReader {
private:
    HANDLE hSerial_; // Handle Windows per porta seriale
    std::string port_name_;
    bool connected_;

public:
    SerialReader() : hSerial_(INVALID_HANDLE_VALUE), connected_(false) {}

    ~SerialReader() {
        disconnect();
    }

    // Rilevamento porta a cui è collegato il pico
    bool auto_detect_pico_port() {
        std::vector<std::string> com_ports = enumerate_com_ports();

        for (const auto& port : com_ports) {
            std::cout << "Tentativo connessione a " << port << "..." << std::endl;

            if (connect(port)) {
                if (test_pico_communication()) {
                    std::cout << "Pico rilevato su porta " << port << std::endl;
                    return true;
                }
                disconnect();
            }
        }

        std::cout << "Nessun Pico trovato. Usando modalità simulata." << std::endl;
        return false;
    }

    // Funzione per l'enumerazione delle porte COM disponibili
    std::vector<std::string> enumerate_com_ports() {
        std::vector<std::string> ports;

        for (int i = 1; i <= 256; i++) {
            std::string port = "COM" + std::to_string(i);
            HANDLE hTest = CreateFileA(port.c_str(), GENERIC_READ | GENERIC_WRITE,
                                       0, NULL, OPEN_EXISTING, 0, NULL);

            if (hTest != INVALID_HANDLE_VALUE) {
                ports.push_back(port);
                CloseHandle(hTest);
            }
        }

        return ports;
    }

    // Connesione porta seriale
    bool connect(const std::string& port) {
        port_name_ = port;

        hSerial_ = CreateFileA(port.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);

        if (hSerial_ == INVALID_HANDLE_VALUE) {
            return false;
        }

        DCB dcbSerialParams = {0};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

        if (!GetCommState(hSerial_, &dcbSerialParams)) {
            CloseHandle(hSerial_);
            hSerial_ = INVALID_HANDLE_VALUE;
            return false;
        }

        dcbSerialParams.BaudRate = CBR_115200;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;
        dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
        dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

        if (!SetCommState(hSerial_, &dcbSerialParams)) {
            CloseHandle(hSerial_);
            hSerial_ = INVALID_HANDLE_VALUE;
            return false;
        }

        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;

        if (!SetCommTimeouts(hSerial_, &timeouts)) {
            CloseHandle(hSerial_);
            hSerial_ = INVALID_HANDLE_VALUE;
            return false;
        }

        connected_ = true;
        return true;
    }

    // Funzione utile in fase di testing per verificare che il pico funzioni corretamente
    bool test_pico_communication() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // Aspetta di più

        // Leggi più linee per catturare qualsiasi output
        for (int i = 0; i < 10; i++) {
            std::string data = read_line();
            std::cout << "Test comunicazione - Linea " << i << ": " << data << std::endl;

            if (!data.empty()) {
                // Accetta qualsiasi output come conferma che il Pico comunica
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        return false;
    }

    void disconnect() {
        if (hSerial_ != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial_);
            hSerial_ = INVALID_HANDLE_VALUE;
        }
        connected_ = false;
    }

    std::string read_line() {
        if (!connected_ || hSerial_ == INVALID_HANDLE_VALUE) {
            return "";
        }

        std::string line;
        char buffer;
        DWORD bytesRead;

        while (ReadFile(hSerial_, &buffer, 1, &bytesRead, NULL) && bytesRead > 0) {
            if (buffer == '\n') {
                break;
            }
            if (buffer != '\r') {
                line += buffer;
            }
        }

        return line;
    }

    bool is_connected() const { return connected_; }
    std::string get_port() const { return port_name_; }
};

class WindowsSecurityHost {
private:
    sqlite3* db_;
    SOCKET server_socket_;
    std::atomic<bool> running_;
    std::mutex data_mutex_;

    // Analytics
    std::vector<SecurityEvent> recent_events_;
    std::map<std::string, float> location_threat_levels_;
    float system_threat_level_;
    uint32_t total_processed_events_;

    // Threads
    std::thread serial_thread_;
    std::thread web_server_thread_;
    std::thread analytics_thread_;

public:
    WindowsSecurityHost() : db_(nullptr), server_socket_(INVALID_SOCKET),
                            running_(true), system_threat_level_(0.0),
                            total_processed_events_(0) {

        std::cout << "Avvio Security Host System - Windows Edition" << std::endl;

        // Inizializza Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "Errore inizializzazione Winsock" << std::endl;
            return;
        }

        // Inizializza database
        if (init_database() != 0) {
            std::cerr << "Errore inizializzazione database" << std::endl;
            return;
        }

        // Avvia threads
        start_threads();

        std::cout << "Sistema host avviato. Dashboard: http://localhost:8080" << std::endl;
    }

    ~WindowsSecurityHost() {
        shutdown_system();
    }

    void run() {
        std::cout << "Security Host operativo. Premi 'q' per terminare." << std::endl;

        while (running_) {
            char input;
            std::cin >> input;
            if (input == 'q' || input == 'Q') {
                running_ = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

private:
    int init_database() {
        int rc = sqlite3_open("security_system.db", &db_);
        if (rc) {
            std::cerr << "Errore apertura database: " << sqlite3_errmsg(db_) << std::endl;
            return rc;
        }

        // Creazione tabelle
        const char* create_events_sql = R"(
            CREATE TABLE IF NOT EXISTS security_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp INTEGER,
                confidence REAL,
                is_pir INTEGER,
                is_vibration INTEGER,
                event_id INTEGER,
                location TEXT,
                processed_by_host INTEGER DEFAULT 1
            );
        )";

        const char* create_alarms_sql = R"(
            CREATE TABLE IF NOT EXISTS security_alarms (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp INTEGER,
                final_confidence REAL,
                trigger_type INTEGER,
                location TEXT,
                threat_level TEXT,
                resolved INTEGER DEFAULT 0
            );
        )";

        rc = sqlite3_exec(db_, create_events_sql, 0, 0, 0);
        rc |= sqlite3_exec(db_, create_alarms_sql, 0, 0, 0);

        if (rc != SQLITE_OK) {
            std::cerr << "Errore creazione tabelle: " << sqlite3_errmsg(db_) << std::endl;
        } else {
            std::cout << "Database SQLite inizializzato" << std::endl;
        }

        return rc;
    }


    void start_threads() {
        serial_thread_ = std::thread(&WindowsSecurityHost::serial_communication_thread, this);
        web_server_thread_ = std::thread(&WindowsSecurityHost::web_server_thread, this);
        analytics_thread_ = std::thread(&WindowsSecurityHost::analytics_thread, this);
    }

    void serial_communication_thread() {
        std::cout << "Thread comunicazione seriale avviato" << std::endl;

        SerialReader serial;
        uint32_t event_counter = 1;
        int connection_attempts = 0;
        const int max_attempts = 3;

        // Tentativi limitati di connessione
        while (running_ && connection_attempts < max_attempts) {
            if (serial.auto_detect_pico_port()) {
                std::cout << "Modalità REALE: Pico connesso su " << serial.get_port() << std::endl;
                break;
            }

            connection_attempts++;
            if (connection_attempts < max_attempts) {
                std::cout << "Tentativo " << connection_attempts << "/" << max_attempts
                          << " fallito. Nuovo tentativo..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }

        if (connection_attempts >= max_attempts) {
            std::cout << "Pico non trovato dopo " << max_attempts
                      << " tentativi. Passaggio a modalità simulata." << std::endl;
        }

        bool using_real_data = (connection_attempts < max_attempts && serial.is_connected());

        while (running_) {
            if (using_real_data && serial.is_connected()) {
                // Gestione dati reali (come prima)
                std::string line = serial.read_line();
                if (!line.empty()) {
                    SecurityEvent event;
                    if (parse_pico_data(line, event, event_counter++)) {
                        process_security_event(event);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            } else {
                // Modalità simulata (come prima)
                int delay = 5000 + (rand() % 10000);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));

                if (!running_) break;

                SecurityEvent event;
                event.timestamp = static_cast<uint32_t>(time(nullptr));
                event.event_id = event_counter++;
                event.is_pir = (rand() % 100) < 80;
                event.is_vibration = (rand() % 100) < 20;
                event.confidence = 60.0f + (rand() % 35);
                event.location = "zona_sim_" + std::to_string((rand() % 3) + 1);

                process_security_event(event);
            }
        }
    }

    void process_security_event(const SecurityEvent& event) {
        std::lock_guard<std::mutex> lock(data_mutex_);

        // Analytics host-side avanzate
        float host_confidence_boost = 0.0;

        // Pattern temporale
        if (recent_events_.size() >= 3) {
            int recent_count = 0;
            for (const auto& recent : recent_events_) {
                if (event.timestamp - recent.timestamp < 30) {
                    recent_count++;
                }
            }

            if (recent_count >= 2) {
                host_confidence_boost += 15.0;
                std::cout << "Pattern clustering rilevato" << std::endl;
            }
        }

        // Multi-sensor fusion
        if (event.is_pir && event.is_vibration) {
            host_confidence_boost += 20.0;
            std::cout << "Multi-sensor fusion" << std::endl;
        }

        // Aggiorna threat level location
        float final_confidence = event.confidence + host_confidence_boost;
        location_threat_levels_[event.location] =
                (location_threat_levels_[event.location] * 0.8) + (final_confidence * 0.2);

        // Salva evento
        save_event_to_database(event);

        // Aggiorna statistiche
        recent_events_.push_back(event);
        if (recent_events_.size() > 50) {
            recent_events_.erase(recent_events_.begin());
        }
        total_processed_events_++;

        // Genera allarme se necessario
        if (final_confidence >= 90.0) {
            generate_host_alarm(event, final_confidence, host_confidence_boost);
        }

        std::cout << "Evento processato: ID=" << event.event_id
                  << ", Conf=" << event.confidence << "% -> " << final_confidence
                  << "%, Loc=" << event.location << std::endl;
    }

    void generate_host_alarm(const SecurityEvent& event, float final_confidence, float host_boost) {
        AlarmEvent alarm;
        alarm.timestamp = event.timestamp;
        alarm.final_confidence = final_confidence;
        alarm.trigger_type = (event.is_pir && event.is_vibration) ? 2 : (event.is_pir ? 0 : 1);
        alarm.location = event.location;

        if (final_confidence >= 95.0) alarm.threat_level = "CRITICO";
        else if (final_confidence >= 90.0) alarm.threat_level = "ALTO";
        else if (final_confidence >= 80.0) alarm.threat_level = "MEDIO";
        else alarm.threat_level = "BASSO";

        save_alarm_to_database(alarm);

        std::cout << "*** ALLARME HOST: " << alarm.threat_level
                  << " - Confidence " << final_confidence << "% ***" << std::endl;
    }

    void save_event_to_database(const SecurityEvent& event) {
        const char* sql = R"(
            INSERT INTO security_events
            (timestamp, confidence, is_pir, is_vibration, event_id, location)
            VALUES (?, ?, ?, ?, ?, ?);
        )";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, event.timestamp);
            sqlite3_bind_double(stmt, 2, event.confidence);
            sqlite3_bind_int(stmt, 3, event.is_pir ? 1 : 0);
            sqlite3_bind_int(stmt, 4, event.is_vibration ? 1 : 0);
            sqlite3_bind_int(stmt, 5, event.event_id);
            sqlite3_bind_text(stmt, 6, event.location.c_str(), -1, SQLITE_STATIC);

            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void save_alarm_to_database(const AlarmEvent& alarm) {
        const char* sql = R"(
            INSERT INTO security_alarms
            (timestamp, final_confidence, trigger_type, location, threat_level)
            VALUES (?, ?, ?, ?, ?);
        )";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, alarm.timestamp);
            sqlite3_bind_double(stmt, 2, alarm.final_confidence);
            sqlite3_bind_int(stmt, 3, alarm.trigger_type);
            sqlite3_bind_text(stmt, 4, alarm.location.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, alarm.threat_level.c_str(), -1, SQLITE_STATIC);

            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void web_server_thread() {
        // Setup server socket
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == INVALID_SOCKET) {
            std::cerr << "Errore creazione socket web server" << std::endl;
            return;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(8080);

        if (bind(server_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            std::cerr << "Errore bind porta 8080" << std::endl;
            return;
        }

        if (listen(server_socket_, 5) == SOCKET_ERROR) {
            std::cerr << "Errore listen socket" << std::endl;
            return;
        }

        std::cout << "Web server avviato su porta 8080" << std::endl;

        while (running_) {
            sockaddr_in client_addr;
            int client_len = sizeof(client_addr);
            SOCKET client_socket = accept(server_socket_, (sockaddr*)&client_addr, &client_len);

            if (client_socket == INVALID_SOCKET) continue;

            // Gestisci richiesta HTTP
            std::thread(&WindowsSecurityHost::handle_http_request, this, client_socket).detach();
        }

        std::cout << "Web server terminato" << std::endl;
    }

    void handle_http_request(SOCKET client_socket) {
        char buffer[4096];
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string request(buffer);

            if (request.find("GET / ") != std::string::npos ||
                request.find("GET /index") != std::string::npos) {
                send_dashboard_html(client_socket);

            } else if (request.find("GET /api/status") != std::string::npos) {
                send_status_json(client_socket);

            } else if (request.find("GET /api/events") != std::string::npos) {
                send_events_json(client_socket);

            } else {
                send_404_response(client_socket);
            }
        }

        closesocket(client_socket);
    }

    void send_dashboard_html(SOCKET client_socket) {
        std::string html = generate_dashboard_html();
        send_http_response(client_socket, "200 OK", "text/html", html);
    }

    void send_status_json(SOCKET client_socket) {
        std::lock_guard<std::mutex> lock(data_mutex_);

        json status;
        status["timestamp"] = static_cast<uint32_t>(time(nullptr));
        status["threat_level"] = system_threat_level_;
        status["total_events"] = total_processed_events_;
        status["recent_events_count"] = recent_events_.size();
        status["active_locations"] = location_threat_levels_.size();

        json locations = json::array();
        for (const auto& loc : location_threat_levels_) {
            json location_info;
            location_info["name"] = loc.first;
            location_info["threat_level"] = loc.second;
            locations.push_back(location_info);
        }
        status["locations"] = locations;

        send_http_response(client_socket, "200 OK", "application/json", status.dump());
    }

    void send_events_json(SOCKET client_socket) {
        std::lock_guard<std::mutex> lock(data_mutex_);

        json events = json::array();
        for (const auto& event : recent_events_) {
            json event_json;
            event_json["timestamp"] = event.timestamp;
            event_json["confidence"] = event.confidence;
            event_json["is_pir"] = event.is_pir;
            event_json["is_vibration"] = event.is_vibration;
            event_json["event_id"] = event.event_id;
            event_json["location"] = event.location;
            events.push_back(event_json);
        }

        send_http_response(client_socket, "200 OK", "application/json", events.dump());
    }

    void send_404_response(SOCKET client_socket) {
        send_http_response(client_socket, "404 Not Found", "text/plain", "Not Found");
    }

    void send_http_response(SOCKET client_socket, const std::string& status,
                            const std::string& content_type, const std::string& body) {
        std::string response = "HTTP/1.1 " + status + "\r\n";
        response += "Content-Type: " + content_type + "\r\n";
        response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
        response += "Connection: close\r\n";
        response += "Access-Control-Allow-Origin: *\r\n";
        response += "\r\n";
        response += body;

        send(client_socket, response.c_str(), static_cast<int>(response.length()), 0);
    }

    std::string generate_dashboard_html() {
        std::string html = "<!DOCTYPE html>\n";
        html += "<html lang=\"it\">\n";
        html += "<head>\n";
        html += "<meta charset=\"UTF-8\">\n";
        html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
        html += "<title>Security System Dashboard</title>\n";
        html += "<style>\n";
        html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }\n";
        html += ".container { max-width: 1200px; margin: 0 auto; }\n";
        html += ".card { background: white; border-radius: 8px; padding: 20px; margin: 20px 0; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
        html += ".header { text-align: center; color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }\n";
        html += ".status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin: 20px 0; }\n";
        html += ".status-item { text-align: center; padding: 20px; background: #f8f9fa; border-radius: 8px; }\n";
        html += ".status-value { font-size: 2em; font-weight: bold; margin: 10px 0; }\n";
        html += ".threat-low { color: #28a745; }\n";
        html += ".threat-medium { color: #ffc107; }\n";
        html += ".threat-high { color: #dc3545; }\n";
        html += ".events-table { width: 100%; border-collapse: collapse; margin-top: 20px; }\n";
        html += ".events-table th, .events-table td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }\n";
        html += ".events-table th { background: #007bff; color: white; }\n";
        html += ".events-table tr:hover { background-color: #f5f5f5; }\n";
        html += ".refresh-btn { background: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; }\n";
        html += ".status-badge { display: inline-block; padding: 4px 8px; border-radius: 4px; font-size: 0.8em; font-weight: bold; }\n";
        html += ".status-online { background: #d4edda; color: #155724; }\n";
        html += "</style>\n";

        // JavaScript
        html += "<script>\n";
        html += "function refreshData() {\n";
        html += "  fetch('/api/status').then(r => r.json()).then(data => {\n";
        html += "    document.getElementById('threat-level').textContent = data.threat_level.toFixed(1) + '%';\n";
        html += "    document.getElementById('total-events').textContent = data.total_events;\n";
        html += "    document.getElementById('recent-events').textContent = data.recent_events_count;\n";
        html += "    document.getElementById('active-locations').textContent = data.active_locations;\n";
        html += "    var el = document.getElementById('threat-level');\n";
        html += "    el.className = 'status-value ' + (data.threat_level > 70 ? 'threat-high' : (data.threat_level > 30 ? 'threat-medium' : 'threat-low'));\n";
        html += "  });\n";
        html += "  fetch('/api/events').then(r => r.json()).then(data => {\n";
        html += "    var tbody = document.getElementById('events-tbody');\n";
        html += "    tbody.innerHTML = '';\n";
        html += "    data.slice(-10).reverse().forEach(event => {\n";
        html += "      var row = tbody.insertRow();\n";
        html += "      var date = new Date(event.timestamp * 1000);\n";
        html += "      row.innerHTML = '<td>' + date.toLocaleString() + '</td><td>' + event.confidence.toFixed(1) + '%</td><td>' + (event.is_pir ? 'PIR ' : '') + (event.is_vibration ? 'VIB' : '') + '</td><td>' + event.location + '</td><td>#' + event.event_id + '</td>';\n";
        html += "    });\n";
        html += "  });\n";
        html += "}\n";
        html += "setInterval(refreshData, 3000);\n";
        html += "window.onload = refreshData;\n";
        html += "</script>\n";
        html += "</head>\n";

        // Body
        html += "<body>\n";
        html += "<div class=\"container\">\n";
        html += "<div class=\"card\">\n";
        html += "<h1 class=\"header\">Security System Dashboard - Windows Edition</h1>\n";
        html += "<div class=\"status-badge status-online\">Host System Online</div>\n";
        html += "</div>\n";
        html += "<div class=\"card\">\n";
        html += "<h2>Stato Sistema</h2>\n";
        html += "<div class=\"status-grid\">\n";
        html += "<div class=\"status-item\"><div>Livello Minaccia</div><div id=\"threat-level\" class=\"status-value threat-low\">0%</div></div>\n";
        html += "<div class=\"status-item\"><div>Eventi Totali</div><div id=\"total-events\" class=\"status-value\">0</div></div>\n";
        html += "<div class=\"status-item\"><div>Eventi Recenti</div><div id=\"recent-events\" class=\"status-value\">0</div></div>\n";
        html += "<div class=\"status-item\"><div>Zone Attive</div><div id=\"active-locations\" class=\"status-value\">0</div></div>\n";
        html += "</div>\n";
        html += "<button class=\"refresh-btn\" onclick=\"refreshData()\">Aggiorna Dati</button>\n";
        html += "</div>\n";
        html += "<div class=\"card\">\n";
        html += "<h2>Eventi Recenti dal Sistema Embedded</h2>\n";
        html += "<table class=\"events-table\">\n";
        html += "<thead><tr><th>Timestamp</th><th>Confidence</th><th>Sensori</th><th>Zona</th><th>ID Evento</th></tr></thead>\n";
        html += "<tbody id=\"events-tbody\"></tbody>\n";
        html += "</table>\n";
        html += "</div>\n";
        html += "</div>\n";
        html += "</body>\n";
        html += "</html>\n";

        return html;
    }

    void analytics_thread() {
        std::cout << "Thread analytics avviato" << std::endl;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(10));

            if (!running_) break;

            std::lock_guard<std::mutex> lock(data_mutex_);

            // Calcola threat level globale
            float base_threat = 0.0;
            if (!recent_events_.empty()) {
                float avg_confidence = 0.0;
                for (const auto& event : recent_events_) {
                    avg_confidence += event.confidence;
                }
                avg_confidence /= recent_events_.size();
                base_threat = avg_confidence * 0.8f;
            }

            system_threat_level_ = (system_threat_level_ * 0.7f) + (base_threat * 0.3f);
            if (system_threat_level_ > 100.0f) system_threat_level_ = 100.0f;

            std::cout << "Analytics: Eventi=" << total_processed_events_
                      << ", Threat=" << system_threat_level_ << "%, Zone="
                      << location_threat_levels_.size() << std::endl;
        }

        std::cout << "Thread analytics terminato" << std::endl;
    }

    void shutdown_system() {
        running_ = false;

        // Chiudi threads
        if (serial_thread_.joinable()) serial_thread_.join();
        if (web_server_thread_.joinable()) web_server_thread_.join();
        if (analytics_thread_.joinable()) analytics_thread_.join();

        // Chiudi socket
        if (server_socket_ != INVALID_SOCKET) {
            closesocket(server_socket_);
        }

        // Chiudi database
        if (db_) {
            sqlite3_close(db_);
        }

        WSACleanup();
        std::cout << "Sistema host terminato correttamente" << std::endl;
    }

    bool parse_pico_data(const std::string& line, SecurityEvent& event, uint32_t event_id) {
        // Parser più aggressivo per catturare tutti i movimenti reali
        std::string line_lower = line;
        std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);

        if (line_lower.find("sensor") != std::string::npos ||
            line_lower.find("pir") != std::string::npos ||
            line_lower.find("allarme") != std::string::npos ) {

            event.timestamp = static_cast<uint32_t>(time(nullptr));
            event.event_id = event_id;
            event.is_pir = line_lower.find("pir") != std::string::npos;
            event.is_vibration = line_lower.find("vib") != std::string::npos ||
                                 line_lower.find("vibr") != std::string::npos;
            event.location = "movimento_reale";

            // Confidence basata sul tipo di evento
            if (line_lower.find("allarme") != std::string::npos) {
                event.confidence = 95.0f;
            } else if (line_lower.find("pattern") != std::string::npos) {
                event.confidence = 90.0f;
            } else if (event.is_pir && event.is_vibration) {
                event.confidence = 92.0f;
            } else if (event.is_pir) {
                event.confidence = 85.0f;
            } else {
                event.confidence = 80.0f;
            }

            return true;
        }

        return false;
    }
};

int main() {
    std::cout << "Sistema distribuito per Raspberry Pi Pico Security System" << std::endl;
    try {
        WindowsSecurityHost host;
        host.run();
    } catch (const std::exception& e) {
        std::cerr << "Errore: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
