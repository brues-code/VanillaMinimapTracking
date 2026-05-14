---@diagnostic disable: undefined-global, create-global
if not C_Minimap then
	DEFAULT_CHAT_FRAME:AddMessage("|cffff4444MinimapBlips:|r VanillaMinimapTracking not found. Addon disabled.")
	return
end

local BLIP_SCALE          = 1.0
local BLIP_SCALE_TRACKING = 1.5

local ROW_HEIGHT = 18
local ROW_WIDTH  = 140
local PADDING    = 6
local BUTTON_RADIUS = 73
local DEFAULT_ANGLE = math.pi

local function Icon(name) return "Interface\\AddOns\\MinimapBlips\\icons\\" .. name end

local E = Enum.MinimapBlip
local BLIP_TYPES = {
	{ type = E.Repair,       label = "Repair",        icon = Icon("Repair"),       		    scale = BLIP_SCALE_TRACKING },
	{ type = E.Vendor,       label = "Vendor",        icon = "Interface\\Icons\\INV_Misc_Coin_02", scale = BLIP_SCALE },
	{ type = E.Innkeeper,    label = "Innkeeper",     icon = Icon("Innkeeper"),               scale = BLIP_SCALE_TRACKING },
	{ type = E.FlightMaster, label = "Flight Master", icon = Icon("FlightMaster"),            scale = BLIP_SCALE_TRACKING },
	{ type = E.Battlemaster, label = "Battlemaster",  icon = Icon("BattleMaster"),            scale = BLIP_SCALE_TRACKING },
	{ type = E.Trainer,      label = "Trainer",       icon = Icon("Profession"),              scale = BLIP_SCALE_TRACKING },
	{ type = E.Auctioneer,   label = "Auctioneer",    icon = Icon("Auctioneer"),              scale = BLIP_SCALE_TRACKING },
	{ type = E.Banker,       label = "Banker",        icon = Icon("Banker"),                  scale = BLIP_SCALE_TRACKING },
	{ type = E.Mailbox,      label = "Mailbox",       icon = Icon("Mailbox"),                 scale = BLIP_SCALE_TRACKING },
	{ type = E.StableMaster, label = "Stable Master", icon = Icon("StableMaster"),            scale = BLIP_SCALE_TRACKING },
	{ type = E.Target,       label = TARGET,          icon = Icon("Target"),                  scale = BLIP_SCALE_TRACKING },
	{ type = E.Focus,        label = FOCUS,           icon = Icon("Focus"),                   scale = BLIP_SCALE_TRACKING },
}
C_Minimap.RegisterIcons(BLIP_TYPES)

local function GetBestTrackingTexture()
	local tracked = C_Minimap.GetTracked()
	for i = table.getn(BLIP_TYPES), 1, -1 do
		local entry = BLIP_TYPES[i]
		if tracked[entry.type] then
			return entry.icon
		end
	end
	return Icon("None")
end

local button = CreateFrame("Button", "MinimapIconBlips", Minimap)

function button:ADDON_LOADED()
	MinimapBlipsUI = MinimapBlipsUI or {}

	self:SetWidth(32)
	self:SetHeight(32)
	self:SetFrameStrata("MEDIUM")
	self:SetMovable(true)
	self:RegisterForDrag("LeftButton")

	local function PositionButtonAt(angle)
		self:ClearAllPoints()
		self:SetPoint("CENTER", self:GetParent(), "CENTER",
			BUTTON_RADIUS * math.cos(angle), BUTTON_RADIUS * math.sin(angle))
	end

	PositionButtonAt(MinimapBlipsUI.buttonAngle or DEFAULT_ANGLE)

	local border = self:CreateTexture(nil, "OVERLAY")
	border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
	border:SetWidth(53)
	border:SetHeight(53)
	border:SetPoint("TOPLEFT", self, "TOPLEFT", 0, 0)

	local icon = self:CreateTexture(nil, "BACKGROUND")
	icon:SetWidth(20)
	icon:SetHeight(20)
	icon:SetPoint("CENTER", self, "CENTER", 0, 0)
	icon:SetTexture(GetBestTrackingTexture())

	self:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

	local menu = CreateFrame("Frame", nil, UIParent)
	menu:SetFrameStrata("FULLSCREEN_DIALOG")
	menu:SetWidth(ROW_WIDTH + PADDING * 2)
	-- +1 row for the "Clear All" entry pinned at the top.
	menu:SetHeight((table.getn(BLIP_TYPES) + 1) * ROW_HEIGHT + PADDING * 2)
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
	self:RegisterEvent("MINIMAP_UPDATE_TRACKING")
	self:SetScript("OnEvent", function()
		if event == "MINIMAP_UPDATE_TRACKING" then
			icon:SetTexture(GetBestTrackingTexture())
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
		for i, entry in ipairs(BLIP_TYPES) do
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
			rowIcon:SetTexture(entry.icon)
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

	local function FollowCursorAroundMinimap()
		local parent = this:GetParent()
		local mx, my = parent:GetCenter()
		if not mx then return end
		local cx, cy = GetCursorPosition()
		local scale = parent:GetEffectiveScale()
		cx, cy = cx / scale, cy / scale
		local angle = math.atan2(cy - my, cx - mx)
		PositionButtonAt(angle)
		MinimapBlipsUI.buttonAngle = angle
	end

	self:SetScript("OnDragStart", function()
		if not IsShiftKeyDown() then return end
		GameTooltip:Hide()
		this:SetScript("OnUpdate", FollowCursorAroundMinimap)
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

button:RegisterEvent("ADDON_LOADED")
button:SetScript("OnEvent", function()
	if event == "ADDON_LOADED" and arg1 == "MinimapBlips" then
		this:ADDON_LOADED()
	end
end)
