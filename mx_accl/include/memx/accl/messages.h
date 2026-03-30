// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_MESSAGES_H
#define MX_MESSAGES_H

#pragma once
#include <cstdint>

namespace MX
{
namespace RPC
{

/**
 * @struct SchedulerOptions
 * @brief Configures scheduling behavior for a DFP (Dataflow Program) instance.
 *
 * The SchedulerOptions struct defines runtime scheduling policies for a DFP when
 * operating in shared mode. These options determine when and how the DFP instance
 * should be swapped out based on input availability, queue capacities, or time constraints.
 * They provide fine-grained control over input/output buffering and processing efficiency.
 * These options also include settings for automatic frequency tuning on supported hardware.
 *
 * @var SchedulerOptions::frame_limit
 * The number of frames to process before the DFP is resheduled. Default: 20
 *
 * @var SchedulerOptions::time_limit
 * The maximum idle time (in milliseconds) to wait for new input before forcibly rescheduling the DFP. Default: 250ms
 *
 * @var SchedulerOptions::ifmap_queue_size
 * Capacity of the input feature map (ifmap) queue used by the DFP, shared by all clients. Default: 16
 *
 * @var SchedulerOptions::ofmap_queue_size
 * Capacity of the per-client output feature map (ofmap) queues. Default: 21 (frame_limit + 1)
 *
 * @var SchedulerOptions::autoclock_enabled
 * Enable automatic frequency up/down-clocking when starting DFP, **if supported by the target MX3 hardware**. Default: false
 *
 * @var SchedulerOptions::autoclock_power_limit_mw
 * Power limit in milliwatts. Autoclocking will increase frequency until this power limit is reached. Range: [1000, 14700]. Default: 11500 mW
 *
 * @var SchedulerOptions::autoclock_check_fps_saturation
 * **EXPERIMENTAL** If true, autoclocking will stop increasing, even if there's still power headroom, if FPS has not increased over the last 3 frequency steps. Default: false
 *
 * @var SchedulerOptions::autoclock_sample_interval_ms
 * Interval in milliseconds between power samples while testing autoclocking steps. Default: 50 ms
 *
 * @var SchedulerOptions::autoclock_num_samples
 * Number of power samples to collect (and find the maximum of) before making a decision to stay at the current frequency or increase it. Default: 6
 *
 */
struct SchedulerOptions {
    uint32_t  frame_limit = 20;
    uint32_t  time_limit = 250;
    uint32_t  ifmap_queue_size = 16;
    uint32_t  ofmap_queue_size = 21;
    bool      autoclock_enabled = false;
    uint32_t  autoclock_power_limit_mw = 11500;
    bool      autoclock_check_fps_saturation = false;
    uint32_t  autoclock_sample_interval_ms = 50;
    uint32_t  autoclock_num_samples = 6;
};


// legacy versions of SchedulerOptions so mxa-manager can maintain
// backwards compatibility with older clients
struct SchedulerOptions_v1 {
    uint32_t  frame_limit;
    uint32_t  time_limit;
    bool      stop_on_empty;
    uint32_t  ifmap_queue_size;
    uint32_t  ofmap_queue_size;
};



/**
 * @struct ClientOptions
 * @brief Configures client-side execution behavior, such as FPS smoothing and pacing.
 *
 * The ClientOptions struct defines optional runtime behaviors for individual clients,
 * including frame rate smoothing and target pacing. These settings help regulate how
 * frequently input frames are submitted, which can be important for latency-sensitive
 * or performance-constrained applications.
 *
 * @var ClientOptions::smoothing
 * If true, enables frame rate smoothing to avoid sudden FPS "jumps", at the cost of increased in-to-out latency. Default: false

 * @var ClientOptions::fps_target
 * When smoothing is enabled, the target FPS to smooth towards. mxa-manager will automatically adjust the delay between frames to try to achieve this target FPS. Default: 0 (disabled)
 */
struct ClientOptions {
    bool  smoothing = false;
    float fps_target = 0;
};


/**
 * @struct device_info_t
 * @brief Contains metadata and configuration details about a device, either local or remote.
 *
 * The device_info_t struct provides information about the hardware layout and capabilities
 * of a MemryX device, including chip count, group configuration, frequency, and power access.
 * This is typically used for runtime device querying or system diagnostics.
 *
 * @var device_info_t::chip_count
 * Number of individual chips available on this device.

 * @var device_info_t::current_config
 * The current group configuration mode, defined by MEMX_MPU_GROUP_CONFIG_* constants.

 * @var device_info_t::num_groups
 * Number of groups configured for this device. Each group may contain one or more chips.

 * @var device_info_t::chips_per_group
 * Number of chips assigned to each group in the current configuration.

 * @var device_info_t::can_get_power_data
 * Indicates whether power telemetry (e.g., voltage, frequency) can be retrieved from the device.

 * @var device_info_t::is_usb
 * If true, this is an USB device, else it is a PCIe device.

 * @var device_info_t::freqs
 * Operating frequency (in MHz) of each chip on the device. The vector contains one entry per chip.

 * @var device_info_t::volt
 * Supply voltage (in millivolts) applied uniformly across all chips.
 */
struct device_info_t {
    int32_t               chip_count;
    int32_t               current_config;
    int32_t               num_groups;
    int32_t               chips_per_group;
    bool                  can_get_power_data;
    bool                  is_usb;
    std::vector<uint16_t> freqs;
    uint16_t              volt;
};



typedef enum : uint32_t {
    MSG_TYPE_CONN,
    MSG_TYPE_FMAP,
    MSG_TYPE_FMAP_IN_INIT,
    MSG_TYPE_FMAP_OUT_INIT,
    MSG_TYPE_LOCK,
    MSG_TYPE_DFP,
    MSG_TYPE_STATUS,
    MSG_TYPE_GET_TEMP_POWER,
    MSG_TYPE_GET_DEV_INFO,
    MSG_TYPE_GET_UTILIZATION,
    MSG_TYPE_SET_POWERMODE,
    MSG_TYPE_INVALID_MAX
} msg_type_t;

// function to decode msg_type_t to a string with the enum name
inline const char* msgtype2str(msg_type_t t)
{
    switch (t) {
        case MSG_TYPE_CONN: return "MSG_TYPE_CONN";
        case MSG_TYPE_FMAP: return "MSG_TYPE_FMAP";
        case MSG_TYPE_FMAP_IN_INIT: return "MSG_TYPE_FMAP_IN_INIT";
        case MSG_TYPE_FMAP_OUT_INIT: return "MSG_TYPE_FMAP_OUT_INIT";
        case MSG_TYPE_LOCK: return "MSG_TYPE_LOCK";
        case MSG_TYPE_DFP: return "MSG_TYPE_DFP";
        case MSG_TYPE_STATUS: return "MSG_TYPE_STATUS";
        case MSG_TYPE_GET_TEMP_POWER: return "MSG_TYPE_GET_TEMP_POWER";
        case MSG_TYPE_GET_DEV_INFO: return "MSG_TYPE_GET_DEV_INFO";
        case MSG_TYPE_GET_UTILIZATION: return "MSG_TYPE_GET_UTILIZATION";
        case MSG_TYPE_SET_POWERMODE: return "MSG_TYPE_SET_POWERMODE";
        default: return "UNKNOWN_MSG_TYPE";
    }
}

// Message header struct
struct MsgHeader {
    uint32_t    client_id;    // Unique client identifier. '0' represents the Server (invalid if client sends 0)
    msg_type_t  msg_type;
};

// lock/unlock packet for Local mode control
struct MsgLocalLock {
    int32_t  device_id;
    enum : uint8_t {
        UNLOCK = 0,
        LOCK = 1,
        TRYLOCK = 2
    } u0_l1_t2;
};

// dfp submission packet (latest protocol)
struct MsgSubmitDfp {
    // Model index
    int32_t  submodel_id;

    // Scheduler options (v2 proto)
    uint32_t time_limit;
    uint32_t frame_limit;
    uint32_t ifmap_queue_size;
    uint32_t ofmap_queue_size;
    uint8_t  autoclock_enabled;
    uint32_t autoclock_power_limit_mw;
    uint8_t  autoclock_check_fps_saturation;
    uint32_t autoclock_sample_interval_ms;
    uint32_t autoclock_num_samples;

    // Client options
    uint8_t  smoothing;
    float    fps_target;

    // Device IDs to use
    int32_t   len_devices_to_use;
    int32_t*  devices_to_use;

    // DFP bytes
    uint64_t num_dfp_bytes;
    uint8_t*  dfp_bytes;
};


// dfp submission packet (older v1 protocol for backwards compatibility)
struct MsgSubmitDfp_v1 {
    // Model index
    int32_t  submodel_id;

    // Scheduler options (v1 proto)
    uint32_t time_limit;
    uint64_t frame_limit;
    uint8_t  stop_on_empty; // (REMOVED, DO NOT USE)
    uint32_t ifmap_queue_size;
    uint32_t ofmap_queue_size;

    // Client options
    uint8_t  smoothing;
    float    fps_target;

    // Device IDs to use
    int32_t   len_devices_to_use;
    int32_t*  devices_to_use;

    // DFP bytes
    uint64_t num_dfp_bytes;
    uint8_t*  dfp_bytes;
};

// get temperature or power packet
struct MsgGetTempPower {
    int32_t  device_id;  // Device ID to get temperature from
    enum : uint8_t {
        TEMPERATURE = 0, // Get temperature
        POWER = 1        // Get power
    } type;          // Type: temperature or power
    enum : uint8_t {
        MODULE = 0, // Get temperature/power of the whole module
        PER_CHIP = 1 // Get temperature of each chip on the module [does not apply to power]
    } target;        // Target: module or per-chip
    enum : uint8_t {
        AVERAGED = 0, // Get rolling average temperature/power
        INSTANT = 1   // Get current temperature/power
    } measure_mode;   // Measure mode: averaged or instant
};

// set power mode (frequency for device) packet
struct MsgSetPowermode {
    int32_t  device_id;    // Device ID to set power mode
    uint16_t freq_mhz;     // Frequency in MHz to set
};

// returned temp/power packets
struct MsgTempPower {
    uint32_t num_items; // Number of chips in the response
    float*    values;   // Array of temperature/power values
};

typedef enum : uint32_t {
    // get a client ID and free up this connection (willingly)

    // NOTE: this command is also used to identify protocol
    //       versions! The following are the SDK-protocol mappings:
    //       SDK 2.0, 2.1 -> PROTO_1
    //       SDK 2.2      -> PROTO_2
    //
    // MXA-Manager versions support the following protocol versions:
    // 2.0/2.1: [1]
    // 2.2:     [1,2]

    // protocol version 1
    INIT_CONNECTION_PROTO_1 = 0,
    END_CONNECTION = 1,

    // protocol version 2
    INIT_CONNECTION_PROTO_2 = 20,

} command_t;

typedef enum : uint32_t {
    OK = 0,

    // upon successful INIT_CONNECTION ctrl command
    HERE_IS_YOUR_NEW_ID = 1,

    // general error
    INVALID_DEVICE = 2,

    // control/init messages
    INVALID_CTRL_COMMAND = 100,
    NO_DEVICES_IN_SYSTEM = 101,
    NEED_TO_CONNECT_CTRL_FIRST = 102,
    YOU_LIED_ABOUT_YOUR_ID = 103,

    // lock issues
    LOCK_INVALID_COMMAND = 201,
    LOCK_DEVICE_ALREADY_LOCKED = 202,
    LOCK_UNLOCK_YOU_ARENT_OWNER = 203,

    // DFP issues
    DFP_WRONG_NUMBER_OF_CHIPS = 300,
    DFP_DOWNLOAD_ERROR = 301,
    DFP_PARSE_ERROR = 302,
    DFP_CHECKSUM_COLLISION = 303,
    DFP_CLIENT_ADD_FAILED = 304,
    DFP_SUBMODEL_ID_OUT_OF_BOUNDS = 305,
    DFP_TOO_MANY_OPEN_CONTEXTS = 306,
    DFP_NO_VALID_CONTEXTS = 307,
    DFP_DEVICE_DOESNT_SUPPORT_AUTO_UPCLOCKING = 308,
    DFP_OK_BUT_IGNORING_OPTIONS = 333,

    // Info polling messages
    INFO_DEVICE_DOESNT_DO_POWER = 400,
    INFO_DEVICE_IS_LOCAL_LOCKED = 401,

    // power mode issues
    SET_POWERMODE_INVALID_OPTION = 500

} status_t;

// function to decode status_t to a string with the enum name
inline const char* status2str(status_t s)
{
    switch (s) {
        case OK: return "OK";
        case HERE_IS_YOUR_NEW_ID: return "HERE_IS_YOUR_NEW_ID";
        case INVALID_DEVICE: return "INVALID_DEVICE";
        case INVALID_CTRL_COMMAND: return "INVALID_CTRL_COMMAND";
        case NO_DEVICES_IN_SYSTEM: return "NO_DEVICES_IN_SYSTEM";
        case NEED_TO_CONNECT_CTRL_FIRST: return "NEED_TO_CONNECT_CTRL_FIRST";
        case YOU_LIED_ABOUT_YOUR_ID: return "YOU_LIED_ABOUT_YOUR_ID";
        case LOCK_INVALID_COMMAND: return "LOCK_INVALID_COMMAND";
        case LOCK_DEVICE_ALREADY_LOCKED: return "LOCK_DEVICE_ALREADY_LOCKED";
        case LOCK_UNLOCK_YOU_ARENT_OWNER: return "LOCK_UNLOCK_YOU_ARENT_OWNER";
        case DFP_WRONG_NUMBER_OF_CHIPS: return "DFP_WRONG_NUMBER_OF_CHIPS";
        case DFP_DOWNLOAD_ERROR: return "DFP_DOWNLOAD_ERROR";
        case DFP_PARSE_ERROR: return "DFP_PARSE_ERROR";
        case DFP_CHECKSUM_COLLISION: return "DFP_CHECKSUM_COLLISION";
        case DFP_CLIENT_ADD_FAILED: return "DFP_CLIENT_ADD_FAILED";
        case DFP_NO_VALID_CONTEXTS: return "DFP_NO_VALID_CONTEXTS";
        case DFP_SUBMODEL_ID_OUT_OF_BOUNDS: return "DFP_SUBMODEL_ID_OUT_OF_BOUNDS";
        case DFP_TOO_MANY_OPEN_CONTEXTS: return "DFP_TOO_MANY_OPEN_CONTEXTS";
        case DFP_DEVICE_DOESNT_SUPPORT_AUTO_UPCLOCKING: return "DFP_DEVICE_DOESNT_SUPPORT_AUTO_UPCLOCKING";
        case DFP_OK_BUT_IGNORING_OPTIONS: return "DFP_OK_BUT_IGNORING_OPTIONS";
        case INFO_DEVICE_DOESNT_DO_POWER: return "INFO_DEVICE_DOESNT_DO_POWER";
        case INFO_DEVICE_IS_LOCAL_LOCKED: return "INFO_DEVICE_IS_LOCAL_LOCKED";
        case SET_POWERMODE_INVALID_OPTION: return "SET_POWERMODE_INVALID_OPTION";
        default: return "UNKNOWN_STATUS";
    }
}

// connect/disconnect packets
struct MsgConnect {
    command_t  cmd;
};


// status return type
struct MsgStatus {
    status_t  s;
    uint32_t  dat;
};

}
}

#endif // MX_MESSAGES_H
