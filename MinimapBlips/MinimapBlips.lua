---@diagnostic disable: undefined-global, create-global

local eventFrame = CreateFrame("Frame")

local function MinimapBlips_OnAddonLoaded()
	if not C_Minimap then
		DEFAULT_CHAT_FRAME:AddMessage("|cffff4444MinimapBlips:|r VanillaMinimapTracking not found. Addon disabled.")
		return
	end

	-- Per-character UI state. Declared in the .toc as SavedVariablesPerCharacter,
	-- so it's WoW-managed (flushed on logout / `/reload`). Purely the addon's
	-- own UI prefs — tracking categories live in the DLL's config file.
	MinimapBlipsUI = MinimapBlipsUI or {}

	local BUTTON_RADIUS = 73
	local DEFAULT_ANGLE = math.pi -- left of minimap; matches the original (-73, 0) anchor

	local button = CreateFrame("Button", "MinimapIconBlips", Minimap)
	button:SetWidth(32)
	button:SetHeight(32)
	button:SetFrameStrata("MEDIUM")
	button:SetMovable(true)
	button:RegisterForDrag("LeftButton")

	local function PositionButtonAt(angle)
		button:ClearAllPoints()
		button:SetPoint("CENTER", Minimap, "CENTER",
			BUTTON_RADIUS * math.cos(angle), BUTTON_RADIUS * math.sin(angle))
	end

	PositionButtonAt(MinimapBlipsUI.buttonAngle or DEFAULT_ANGLE)

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

	-- Snaps the button to the minimap edge at the angle from the minimap
	-- center to the cursor. Called per OnUpdate while dragging — keeps the
	-- button orbiting on the same radius instead of drifting off in straight
	-- lines like a free-positioned frame would. Persists the angle as it
	-- goes so the drop position survives logout / `/reload`.
	local function FollowCursorAroundMinimap()
		local mx, my = Minimap:GetCenter()
		if not mx then return end
		local cx, cy = GetCursorPosition()
		local scale = Minimap:GetEffectiveScale()
		cx, cy = cx / scale, cy / scale
		local angle = math.atan2(cy - my, cx - mx)
		PositionButtonAt(angle)
		MinimapBlipsUI.buttonAngle = angle
	end

	button:SetScript("OnClick", function()
		-- Shift-click is reserved for dragging — swallow it so the menu
		-- doesn't pop up when the user releases without having dragged.
		if IsShiftKeyDown() then return end
		MinimapBlipsMenu_Toggle(this)
	end)

	button:SetScript("OnDragStart", function()
		if not IsShiftKeyDown() then return end
		GameTooltip:Hide()
		this:SetScript("OnUpdate", FollowCursorAroundMinimap)
	end)

	button:SetScript("OnDragStop", function()
		this:SetScript("OnUpdate", nil)
	end)

	button:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_LEFT")
		GameTooltip:SetText("Minimap Blips")
		GameTooltip:AddLine("Shift-drag to move.", 0.7, 0.7, 0.7)
		GameTooltip:Show()
	end)

	button:SetScript("OnLeave", function()
		GameTooltip:Hide()
	end)

	MinimapBlipsMenu_RegisterIcons()
end

eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:SetScript("OnEvent", function()
	if event == "ADDON_LOADED" and arg1 == "MinimapBlips" then
		MinimapBlips_OnAddonLoaded()
		eventFrame:UnregisterEvent("ADDON_LOADED")
	end
end)
