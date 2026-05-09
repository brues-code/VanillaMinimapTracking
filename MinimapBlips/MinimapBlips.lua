---@diagnostic disable: undefined-global, create-global

local eventFrame = CreateFrame("Frame")

local function MinimapBlips_OnAddonLoaded()
	if not C_MinimapBlip then
		DEFAULT_CHAT_FRAME:AddMessage("|cffff4444MinimapBlips:|r VanillaMinimapTracking not found. Addon disabled.")
		return
	end

	local button = CreateFrame("Button", "MinimapIconBlips", Minimap)
	button:SetWidth(32)
	button:SetHeight(32)
	button:SetFrameStrata("MEDIUM")
	button:SetPoint("CENTER", Minimap, "CENTER", -73, 0)

	local border = button:CreateTexture(nil, "OVERLAY")
	border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
	border:SetWidth(53)
	border:SetHeight(53)
	border:SetPoint("TOPLEFT", button, "TOPLEFT", 0, 0)

	local icon = button:CreateTexture("MinimapIconBlipsIcon", "BACKGROUND")
	icon:SetWidth(20)
	icon:SetHeight(20)
	icon:SetPoint("CENTER", button, "CENTER", 1, 1)
	icon:SetTexture("Interface\\Icons\\INV_Misc_Map_01")
	icon:SetTexCoord(0.05, 0.95, 0.05, 0.95)

	button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

	button:SetScript("OnClick", function()
		MinimapBlipsMenu_Toggle(this)
	end)

	button:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_LEFT")
		GameTooltip:SetText("Minimap Blips")
		GameTooltip:Show()
	end)

	button:SetScript("OnLeave", function()
		GameTooltip:Hide()
	end)

	MinimapBlipsMenu_RegisterIcons()
end

local function MinimapBlips_OnPlayerLogin()
	if not C_MinimapBlip then return end
	-- Querying IsTracked is what triggers the DLL's lazy config load on the
	-- first call after login; we just need the menu's current state synced.
	if MinimapBlipsMenu_RefreshAll then MinimapBlipsMenu_RefreshAll() end
end

eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("PLAYER_LOGIN")
eventFrame:SetScript("OnEvent", function()
	if event == "ADDON_LOADED" and arg1 == "MinimapBlips" then
		MinimapBlips_OnAddonLoaded()
	elseif event == "PLAYER_LOGIN" then
		MinimapBlips_OnPlayerLogin()
	end
end)
