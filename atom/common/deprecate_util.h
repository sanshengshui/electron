// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_COMMON_DEPRECATE_UTIL_H_
#define ATOM_COMMON_DEPRECATE_UTIL_H_

#include <string>

#include "atom/common/node_includes.h"
#include "base/optional.h"

namespace atom {

bool EmitDeprecationWarning(
    node::Environment* env,
    const std::string& warning_msg,
    const base::Optional<std::string>& warning_type = base::nullopt,
    const base::Optional<std::string>& warning_code = base::nullopt);

}  // namespace atom

#endif  // ATOM_COMMON_DEPRECATE_UTIL_H_
