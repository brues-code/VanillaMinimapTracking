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

namespace Blips {

void Save();
void Reset();

// Set true on entry to `CGGameUI_Shutdown_h` (logout / `/reload`) so any of
// our hooks the engine fires during teardown can bail out instead of
// running their normal logic. The engine's logout sequence can drive code
// paths that crash inside `ClntObjMgrEnumVisibleObjects` once the object
// manager has been torn down — even though we've installed null-checks at
// every direct call site, defensive code that touches engine state
// during this window has caused observed crashes.
extern volatile bool g_inLogout;

} // namespace Blips
