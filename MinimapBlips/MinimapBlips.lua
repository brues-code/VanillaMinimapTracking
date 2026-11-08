---@diagnostic disable: undefined-global, create-global
if not C_Minimap then
	DEFAULT_CHAT_FRAME:AddMessage("|cffff4444MinimapBlips:|r VanillaMinimapTracking not found. Addon disabled.")
	return
end

local ROW_HEIGHT = 18
local ROW_WIDTH  = 140
local PADDING    = 6
local BUTTON_RADIUS = 73
local DEFAULT_ANGLE = math.pi

local NONE_ICON = "Interface\\AddOns\\MinimapBlips\\icons\\None"

local RAID_MARK_TYPE = "raid mark"
-- Skull = raid icon 8: col 3, row 1 of the 4x4 atlas (0.25 per cell).
local SKULL_TEXCOORD = { 0.75, 1.0, 0.25, 0.5 }

-- Party / raid dots are white textures the DLL tints per member with the
-- class colour. In the menu and on the button we tint them with the player's
-- own class colour so they read as "a class-coloured dot".
local GROUP_TYPES = { ["party"] = true, ["raid"] = true }

local function PlayerClassColor()
	local _, token = UnitClass("player")
	local c = token and RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
	if c then return c.r, c.g, c.b end
	return 1, 1, 1
end

-- Hands the live RAID_CLASS_COLORS table to the DLL so dot colours follow
-- whatever the UI (or a class-colour mod) has set.
local function PushClassColors()
	if not RAID_CLASS_COLORS or not C_Minimap.SetClassColor then return end
	for token, c in pairs(RAID_CLASS_COLORS) do
		if type(token) == "string" and type(c) == "table" and c.r and c.g and c.b then
			C_Minimap.SetClassColor(token, c.r, c.g, c.b)
		end
	end
end

local function ApplyBlipIcon(texture, iconPath, blipType)
	texture:SetTexture(iconPath)
	if blipType == RAID_MARK_TYPE then
		texture:SetTexCoord(SKULL_TEXCOORD[1], SKULL_TEXCOORD[2], SKULL_TEXCOORD[3], SKULL_TEXCOORD[4])
	else
		texture:SetTexCoord(0, 1, 0, 1)
	end
	if blipType and GROUP_TYPES[blipType] then
		texture:SetVertexColor(PlayerClassColor())
	else
		texture:SetVertexColor(1, 1, 1)
	end
end

-- Default icon set lives in the DLL now (see kBlipTypes in Blips.cpp); we
-- just read it back. The DLL pre-registers everything at module init, so
-- GetIconList is ready by the time this addon runs.
local ICONS = C_Minimap.GetIconList()
local NUM_BLIPS = table.getn(ICONS)

-- Returns the icon path + type of the highest-priority tracked category, or
-- the "none" icon (and nil type) when nothing is tracked.
local function GetBestTracking()
	local tracked = C_Minimap.GetTracked()
	for i = NUM_BLIPS, 1, -1 do
		local entry = ICONS[i]
		if tracked[entry.type] then
			return entry.icon, entry.type
		end
	end
	return NONE_ICON, nil
end

local button = CreateFrame("Button", "MinimapIconBlips", Minimap)

function button:ADDON_LOADED()
	MinimapBlipsUI = MinimapBlipsUI or {}

	self:SetWidth(32)
	self:SetHeight(32)
	self:SetFrameStrata("MEDIUM")
	self:SetMovable(true)
	self:RegisterForDrag("LeftButton")
	self:SetClampedToScreen(true)

	local function RestoreButtonPosition()
		self:ClearAllPoints()
		local pos = MinimapBlipsUI.buttonPos
		if pos then
			self:SetPoint("CENTER", self:GetParent(), "CENTER", pos.x, pos.y)
		else
			self:SetPoint("CENTER", self:GetParent(), "CENTER",
				BUTTON_RADIUS * math.cos(DEFAULT_ANGLE), BUTTON_RADIUS * math.sin(DEFAULT_ANGLE))
		end
	end

	RestoreButtonPosition()

	local border = self:CreateTexture(nil, "OVERLAY")
	border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
	border:SetWidth(53)
	border:SetHeight(53)
	border:SetPoint("TOPLEFT", self, "TOPLEFT", 0, 0)

	local icon = self:CreateTexture(nil, "BACKGROUND")
	icon:SetWidth(20)
	icon:SetHeight(20)
	icon:SetPoint("CENTER", self, "CENTER", 0, 0)
	ApplyBlipIcon(icon, GetBestTracking())

	self:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

	local menu = CreateFrame("Frame", nil, UIParent)
	menu:SetFrameStrata("FULLSCREEN_DIALOG")
	menu:SetWidth(ROW_WIDTH + PADDING * 2)
	-- +1 row for the "Clear All" entry pinned at the top.
	menu:SetHeight((NUM_BLIPS + 1) * ROW_HEIGHT + PADDING * 2)
	menu:SetBackdrop({
		bgFile   = "Interface\\Tooltips\\UI-Tooltip-Background",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		tile     = true,
		tileSize = 16,
		edgeSize = 16,
		insets   = { left = 4, right = 4, top = 4, bottom = 4 },
	})
	menu:SetBackdropColor(0.09, 0.09, 0.09, 0.9)
	menu:SetBackdropBorderColor(0.4, 0.4, 0.4, 1)
	menu:Hide()
	PushClassColors()

	self:RegisterEvent("MINIMAP_UPDATE_TRACKING")
	self:RegisterEvent("PLAYER_FOCUS_CHANGED")
	-- Re-push on entering the world: class-colour mods often rewrite
	-- RAID_CLASS_COLORS after we load; also UnitClass("player") is reliable
	-- by then for the button tint.
	self:RegisterEvent("PLAYER_ENTERING_WORLD")
	self:SetScript("OnEvent", function()
		if event == "MINIMAP_UPDATE_TRACKING" then
			ApplyBlipIcon(icon, GetBestTracking())
		elseif event == "PLAYER_FOCUS_CHANGED" then
			self:FOCUS_CHANGED()
		elseif event == "PLAYER_ENTERING_WORLD" then
			PushClassColors()
			ApplyBlipIcon(icon, GetBestTracking())
		end
	end)

	local function GenerateRows()
		if menu.rows then return end
		menu.rows = true
		local clearRow = CreateFrame("Button", nil, menu)
		clearRow:SetWidth(ROW_WIDTH)
		clearRow:SetHeight(ROW_HEIGHT)
		clearRow:SetPoint("TOPLEFT", menu, "TOPLEFT", PADDING, -PADDING)
		clearRow:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
		local clearLabel = clearRow:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		clearLabel:SetPoint("CENTER", clearRow, "CENTER", 0, 0)
		clearLabel:SetText("Clear All")
		clearRow:SetScript("OnClick", function() C_Minimap.ClearAllTracking() end)

		local tracked = C_Minimap.GetTracked()
		for i, entry in ipairs(ICONS) do
			local row = CreateFrame("Button", nil, menu)
			row.blipType = entry.type
			row:SetWidth(ROW_WIDTH)
			row:SetHeight(ROW_HEIGHT)
			row:SetPoint("TOPLEFT", menu, "TOPLEFT", PADDING, -(PADDING + i * ROW_HEIGHT))
			row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")

			row.check = row:CreateTexture(nil, "ARTWORK")
			row.check:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
			row.check:SetWidth(14)
			row.check:SetHeight(14)
			row.check:SetPoint("LEFT", row, "LEFT", 0, 0)
			if not tracked[entry.type] then
				row.check:Hide()
			end

			local rowIcon = row:CreateTexture(nil, "ARTWORK")
			ApplyBlipIcon(rowIcon, entry.icon, entry.type)
			rowIcon:SetWidth(14)
			rowIcon:SetHeight(14)
			rowIcon:SetPoint("LEFT", row, "LEFT", 18, 0)

			local label = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
			label:SetPoint("LEFT", row, "LEFT", 36, 0)
			label:SetText(entry.label)

			row:SetScript("OnClick", function()
				C_Minimap.Toggle(this.blipType)
			end)

			row:SetScript("OnEvent", function()
				if event == "MINIMAP_UPDATE_TRACKING" then
					if arg1 == "" or arg1 == this.blipType then
						if C_Minimap.IsTracked(this.blipType) then
							this.check:Show()
						else
							this.check:Hide()
						end
					end
				end
			end)
			row:RegisterEvent("MINIMAP_UPDATE_TRACKING")
		end
	end

	self:SetScript("OnClick", function()
		if IsShiftKeyDown() then return end
		if menu:IsShown() then
			menu:Hide()
			return
		end
		GenerateRows()
		menu:ClearAllPoints()
		menu:SetPoint("TOPRIGHT", self, "TOPLEFT", 0, -5)
		menu:Show()
	end)

	local function FollowCursor()
		local parent = this:GetParent()
		local mx, my = parent:GetCenter()
		if not mx then return end
		local scale = parent:GetEffectiveScale()
		local cx, cy = GetCursorPosition()
		local x, y = cx / scale - mx, cy / scale - my
		this:ClearAllPoints()
		this:SetPoint("CENTER", parent, "CENTER", x, y)
		MinimapBlipsUI.buttonPos = { x = x, y = y }
	end

	self:SetScript("OnDragStart", function()
		if not IsShiftKeyDown() then return end
		GameTooltip:Hide()
		this:SetScript("OnUpdate", FollowCursor)
	end)

	self:SetScript("OnDragStop", function()
		this:SetScript("OnUpdate", nil)
	end)

	self:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_LEFT")
		GameTooltip:SetText("Minimap Blips")
		GameTooltip:AddLine("Shift-drag to move.", 0.7, 0.7, 0.7)
		GameTooltip:Show()
	end)

	self:SetScript("OnLeave", function()
		GameTooltip:Hide()
	end)
	self:UnregisterEvent("ADDON_LOADED")
end

function button:FOCUS_CHANGED()
	local unitGUID = UnitGUID('focus')
	if unitGUID then
		C_Minimap.SetFocusByGUID(unitGUID)
	else
		C_Minimap.ClearFocus()
	end
end

button:RegisterEvent("ADDON_LOADED")
button:SetScript("OnEvent", function()
	if event == "ADDON_LOADED" and arg1 == "MinimapBlips" then
		this:ADDON_LOADED()
	end
end)
