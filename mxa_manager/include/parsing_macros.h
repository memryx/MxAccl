// Copyright (c) 2025 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PARSING_MACROS_H
#define PARSING_MACROS_H

#pragma once


#define RECV_DFP_PROTO_V1 \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.submodel_id), sizeof(msg_dfp_v1.submodel_id)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.time_limit), sizeof(msg_dfp_v1.time_limit)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.frame_limit), sizeof(msg_dfp_v1.frame_limit)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.stop_on_empty), sizeof(msg_dfp_v1.stop_on_empty)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.ifmap_queue_size), sizeof(msg_dfp_v1.ifmap_queue_size)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.ofmap_queue_size), sizeof(msg_dfp_v1.ofmap_queue_size)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.smoothing), sizeof(msg_dfp_v1.smoothing)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v1.fps_target), sizeof(msg_dfp_v1.fps_target)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            }


//================================================================================================================================//

#define RECV_DFP_PROTO_V2 \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.submodel_id), sizeof(msg_dfp_v2.submodel_id)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.time_limit), sizeof(msg_dfp_v2.time_limit)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.frame_limit), sizeof(msg_dfp_v2.frame_limit)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.ifmap_queue_size), sizeof(msg_dfp_v2.ifmap_queue_size)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.ofmap_queue_size), sizeof(msg_dfp_v2.ofmap_queue_size)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.autoclock_enabled), sizeof(msg_dfp_v2.autoclock_enabled)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.autoclock_power_limit_mw), sizeof(msg_dfp_v2.autoclock_power_limit_mw)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.autoclock_check_fps_saturation), sizeof(msg_dfp_v2.autoclock_check_fps_saturation)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.autoclock_sample_interval_ms), sizeof(msg_dfp_v2.autoclock_sample_interval_ms)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.autoclock_num_samples), sizeof(msg_dfp_v2.autoclock_num_samples)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.smoothing), sizeof(msg_dfp_v2.smoothing)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            } \
            rbytes = s->read(mxasio::buffer(&(msg_dfp_v2.fps_target), sizeof(msg_dfp_v2.fps_target)), error); \
            if(rbytes == 0 || error) { \
                spdlog::error("[CTRL] control connection from {} error: {}", s->remote_endpoint(), error.message()); \
                break; \
            }


//================================================================================================================================//

#define CLEAN_MSG_DFP \
         delete [] msg_dfp_dfp_bytes; \
         msg_dfp_dfp_bytes = nullptr; \
         delete [] msg_dfp_devices_to_use; \
         msg_dfp_devices_to_use = nullptr;


#endif // PARSING_MACROS_H
