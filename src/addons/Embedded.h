// This file is part of VanillaMinimapTracking.
//
// VanillaMinimapTracking is free software: you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// VanillaMinimapTracking is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// VanillaMinimapTracking. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <windows.h>

namespace Addons::Embedded {

// Installs the file-read + addon-init hooks that make the engine treat
// `MinimapBlips` as an installed addon even when nothing is on disk. Must
// be called from DllMain after `MH_Initialize`. Returns FALSE (via the
// HOOK_FUNCTION macro's early-out) on hook failure.
BOOL InstallHooks();

} // namespace Addons::Embedded
