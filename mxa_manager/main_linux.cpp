// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <fstream>
#include <filesystem>

#include "spdlog/spdlog.h"
#include "spdlog/cfg/env.h"

#include <memx/accl/utils/cpu_opts.h>

MX::Manager::Server* server;

// ctrl+c handler to stop the server gracefully
void signal_handler(int signum)
{
    spdlog::warn("[Server] Caught signal {}: stopping server...", signum);
    if(server) {
        delete server; // will call Server::kill() internally
        server = nullptr;
        exit(EXIT_SUCCESS);
    }
    else {
        spdlog::error("[Server] Server pointer is null, cannot stop server gracefully");
        exit(EXIT_FAILURE);
    }
}


// function to parse the mxa_manager.conf file from
// a fixed path location: one for Linux and one for Windows
bool parse_config_file(std::string* addr, unsigned short* base_port, std::string* log_level, unsigned int* hw_monitor_interval)
{
    std::string config_path;
    config_path = "/etc/memryx/mxa_manager.conf";

    if(!std::filesystem::exists(config_path)) {
        spdlog::critical("Config file not found at {}", config_path);
        return false;
    }
    std::ifstream config_file(config_path);
    if(!config_file.is_open()) {
        spdlog::critical("Unable to open config file at {}", config_path);
        return false;
    }
    std::string line;

    // the syntax of the config file ignores all lines starting with #,
    // then looks for these variables:
    // LISTEN_ADDRESS="address_as_string"
    // BASE_PORT=port_as_integer
    // LOG_LEVEL=level
    // HW_MONITOR_INTERVAL=interval_in_milliseconds
    //
    // addr is then assgined to the address string and base_port to the port integer
    bool found_addr = false;
    bool found_port = false;
    while(std::getline(config_file, line)) {
        if(line[0] == '#') { continue; } // ignore comments
        if(line.find("LISTEN_ADDRESS=") != std::string::npos) {
            *addr = line.substr(16, line.length() - 17);
            found_addr = true;
        }
        else if(line.find("BASE_PORT=") != std::string::npos) {
            *base_port = std::stoi(line.substr(10));
            found_port = true;
        }
        else if(line.find("LOG_LEVEL=") != std::string::npos) {
            std::string level = line.substr(10);
            if(level == "debug" || level == "info" || level == "warn" || level == "critical" ||
               level == "high" || level == "med" || level == "medium" || level == "low" ||
               level == "off") {
                *log_level = level;
            }
            else {
                spdlog::warn("Invalid LOG_LEVEL in config file: {}. Using default (low).", level);
                *log_level = "low";
            }
        }
        else if(line.find("HW_MONITOR_INTERVAL=") != std::string::npos) {
            *hw_monitor_interval = std::stoi(line.substr(20));
        }
    }

    if(!found_addr || !found_port) {
        spdlog::critical("Config file is missing required variables: LISTEN_ADDRESS or BASE_PORT");
        return false;
    }

    return true;
}

bool parse_command_line(int argc, char* argv[], std::string* addr, unsigned short* base_port, std::string* log_level, unsigned int* hw_monitor_interval) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // if the arg is only --version or -V, print the version and exit
        if (arg == "--version" || arg == "-V") {
            std::cout << MX::Types::RUNTIME_VERSION << std::endl;
            exit(0);
            return false;
        }

        if ((arg == "--addr" || arg == "-a") && i + 1 < argc) {
            *addr = argv[++i];
        } 
        else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            *base_port = static_cast<unsigned short>(std::stoi(argv[++i]));
        } 
        else if ((arg == "--log" || arg == "-l") && i + 1 < argc) {
            std::string level = argv[++i];

            if(level == "debug" || level == "info" || level == "warn" || level == "critical" ||
               level == "high" || level == "med" || level == "medium" || level == "low" ||
               level == "off") {
                *log_level = level;
            }
            else {
                spdlog::warn("Invalid LOG_LEVEL in config file: {}. Using default (low).", level);
                *log_level = "low";
            }

            *log_level = level;
        } 
        else if ((arg == "--interval" || arg == "-i") && i + 1 < argc) {
            *hw_monitor_interval = std::stoul(argv[++i]);
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: mxa_manager [options]\n"
                      << "  -a, --addr <addr>      Listen address\n"
                      << "  -p, --port <port>      Base port\n"
                      << "  -l, --log <level>      Log level (debug, info, warn, critical)\n"
                      << "  -i, --interval <ms>    HW monitor interval\n"
                      << "  -V, --version          Print version and exit\n";
            exit(0);
            return false;
        }
        else {
            spdlog::error("Unknown command line argument: {}", arg);
            return false;
        }
    }
    return true;
}

// main function parses the config file, creates a Server object,
// then starts the server with .run()
int main(int argc, char* argv[])
{
    std::string addr;
    unsigned short base_port = 10000;
    std::string log_level = "";
    unsigned int hw_monitor_interval = 500; // in milliseconds

    if (argc > 1) {
        // args are fed from command line
        if(!parse_command_line(argc, argv, &addr, &base_port, &log_level, &hw_monitor_interval)){
            return EXIT_FAILURE;
        }
    }
    else {
        if(!parse_config_file(&addr, &base_port, &log_level, &hw_monitor_interval)) {
            return EXIT_FAILURE;
        }
    }


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
    spdlog::set_pattern("[thread %t] [%l]%$ %v");

    // set CPU affinity to big cores, requiring at least 2
    MX::Utils::set_self_affinity_to_big_cores(2);

    // create server object
    server = new MX::Manager::Server(addr, base_port, hw_monitor_interval);

    // set up signal handler for ctrl+c
    std::signal(SIGINT, signal_handler);

    // start the server
    server->start();

    // sleep the main thread forever
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return EXIT_SUCCESS;
}
