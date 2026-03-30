// main_win.cpp
#include <memory>
#include <winsock2.h>
#include <windows.h>
#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/win_eventlog_sink.h>
#include "spdlog/cfg/env.h"
#include "server.h"

#define SVCNAME "MxaManagerSvc"

using MX::Manager::Server;

SERVICE_STATUS_HANDLE g_StatusHandle = nullptr;
HANDLE               g_StopEvent    = INVALID_HANDLE_VALUE;

// Forward declarations
void WINAPI ServiceMain(DWORD argc, LPSTR *argv);
DWORD WINAPI ServiceCtrlHandler(DWORD control, DWORD eventType, LPVOID eventData, LPVOID context);

// Service dispatch table
SERVICE_TABLE_ENTRYA ServiceTableA[] = {
    { const_cast<LPSTR>(SVCNAME), ServiceMain },
    { nullptr, nullptr }
};

void init_logging()
{
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    auto evsink = std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("MxaManagerSvc", 0);
    evsink->set_level(spdlog::level::info);

    std::vector<spdlog::sink_ptr> sinks{ console_sink, evsink };
    auto logger = std::make_shared<spdlog::logger>("mxa", sinks.begin(), sinks.end());
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
}

// function to parse the mxa_manager.conf file from a fixed path location
bool parse_config_file(std::string& addr, unsigned short& base_port, std::string& log_level, unsigned int& hw_monitor_interval)
{
    std::string config_path = "C:\\Program Files\\memryx\\mxa_manager.conf";

    if (!std::filesystem::exists(config_path)) {
        spdlog::critical("Config file not found at {}", config_path);
        return false;
    }
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        spdlog::critical("Unable to open config file at {}", config_path);
        return false;
    }
    std::string line;

    // the syntax of the config file ignores all lines starting with #,
    // then looks for these two variables:
    // LISTEN_ADDRESS="address_as_string"
    // BASE_PORT=port_as_integer
    // LOG_LEVEL=level
    // HW_MONITOR_INTERVAL=interval_in_milliseconds
    //
    // addr is then assgined to the address string and base_port to the port integer
    bool found_addr = false;
    bool found_port = false;
    while (std::getline(config_file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("LISTEN_ADDRESS=", 0) == 0) {
            addr = line.substr(16, line.length() - 17);
            found_addr = true;
        } else if (line.rfind("BASE_PORT=", 0) == 0) {
            base_port = static_cast<unsigned short>(std::stoi(line.substr(10)));
            found_port = true;
        }
        else if(line.find("LOG_LEVEL=") != std::string::npos) {
            std::string level = line.substr(10);
            if(level == "debug" || level == "info" || level == "warn" || level == "critical" ||
               level == "high" || level == "med" || level == "medium" || level == "low" ||
               level == "off") {
                log_level = level;
            }
            else {
                spdlog::warn("Invalid LOG_LEVEL in config file: {}. Using default (low).", level);
                log_level = "low";
            }
        }
        else if(line.find("HW_MONITOR_INTERVAL=") != std::string::npos) {
            hw_monitor_interval = static_cast<unsigned int>(std::stoi(line.substr(20)));
        }
    }

    if (!found_addr || !found_port) {
        spdlog::critical("Config file missing LISTEN_ADDRESS or BASE_PORT");
        return false;
    }

    return true;
}

// Helper to report service status to SCM
void ReportServiceStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD s_dwCheckPoint = 1;
    SERVICE_STATUS status = {};
    status.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState     = currentState;
    status.dwControlsAccepted = (currentState == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;
    status.dwWin32ExitCode    = win32ExitCode;

    if (currentState == SERVICE_START_PENDING ||
        currentState == SERVICE_STOP_PENDING) {
    status.dwWaitHint         = waitHint;
        status.dwCheckPoint = s_dwCheckPoint++;
    } else {
        status.dwWaitHint = 0;
        status.dwCheckPoint = 0;
    }

    SetServiceStatus(g_StatusHandle, &status);
}

// ANSI console entry point -> Service entry
int main(int argc, char *argv[]) {
    if (!StartServiceCtrlDispatcherA(ServiceTableA)) {
        return static_cast<int>(GetLastError());
    }
    return 0;
}

// Called by SCM to start the service
void WINAPI ServiceMain(DWORD argc, LPSTR *argv) {

    init_logging();
    spdlog::info("ServiceMain entered");

    g_StatusHandle = RegisterServiceCtrlHandlerExA(
    	SVCNAME,
    	ServiceCtrlHandler,
    	nullptr
    );
    if (!g_StatusHandle) {
        spdlog::error("RegisterServiceCtrlHandlerExA failed");
        return;
    }
    spdlog::info("Handler registered");

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    spdlog::info("Reported START_PENDING");

    // Create event to handle stop requests
    g_StopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_StopEvent) {
        spdlog::error("CreateEvent failed");
        ReportServiceStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }
    spdlog::info("Stop event created");

    std::string addr = "127.0.0.1";
    unsigned short base_port = 10000;
    std::string log_level = "";
    unsigned int hw_monitor_interval = 500;
    if (!parse_config_file(addr, base_port, log_level, hw_monitor_interval)) {
        spdlog::error("Configuration parse failed", EVENTLOG_ERROR_TYPE);
        ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
    spdlog::info("Starting server on {}:{}", addr, base_port);
    // set up spdlog logging
    if(log_level == "") {
        spdlog::cfg::load_env_levels(); // load log levels from environment variables
    } else {
        spdlog::level::level_enum level;
        if(log_level == "debug" || log_level == "high") {
            level = spdlog::level::debug;
        } else if(log_level == "info" || log_level == "med" || log_level == "medium") {
            level = spdlog::level::info;
        } else if(log_level == "warn" || log_level == "low") {
            level = spdlog::level::warn;
        } else if(log_level == "critical" || log_level == "off") {
            level = spdlog::level::critical;
        } else {
            level = spdlog::level::warn; // default
        }
        spdlog::set_level(level);
    }

    // Report running status
    ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
    spdlog::info("Reported RUNNING");

    // Start the server (blocks until stop event)
    std::unique_ptr<Server> srv = std::make_unique<Server>(addr, base_port, hw_monitor_interval);
    std::thread worker([&srv]() {
        srv->start();
    });
    worker.detach();

    // Wait stop event trigger
    WaitForSingleObject(g_StopEvent, INFINITE);
    spdlog::info("Stop event signaled, exiting");

    // Service is stopping
    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
    spdlog::info("Reported STOPPED");
}

// Handles control requests from SCM
DWORD WINAPI ServiceCtrlHandler(
    DWORD  control,
    DWORD  /*eventType*/,
    LPVOID /*eventData*/,
    LPVOID /*context*/
) {
    if (control == SERVICE_CONTROL_STOP) {
        ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        SetEvent(g_StopEvent);
    }
    return NO_ERROR;
}
