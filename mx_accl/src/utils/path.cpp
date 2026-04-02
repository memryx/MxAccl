// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdlib>
#include <memx/accl/utils/path.h>

namespace fs = std::filesystem;

fs::path MX::Utils::mx_get_home_dir()
{
    fs::path home_path;
    if (const char* env_p = std::getenv("MX_API_HOME")) {
        home_path += env_p;
    }
    if (!home_path.empty()) {
        return home_path;
    }

#ifdef MX_API_HOME_DIR
    if (MX_API_HOME_DIR[0] != '\0') {
        home_path = fs::path(MX_API_HOME_DIR);
        return home_path;
    }
#endif

    // Fallback for dev/test runs: walk up from cwd to find the repo root.
    fs::path cwd = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(cwd / "mx_accl/tests/models")) {
            home_path = cwd;
            return home_path;
        }
        if (!cwd.has_parent_path()) {
            break;
        }
        cwd = cwd.parent_path();
    }
    return home_path;
}

fs::path MX::Utils::mx_get_accl_dir()
{
    fs::path accl_path;
    fs::path home_path = mx_get_home_dir();
    // if (!fs::exists(home_path))
    // {
    accl_path = home_path / "mx_accl";
    // }
    return accl_path;
}
