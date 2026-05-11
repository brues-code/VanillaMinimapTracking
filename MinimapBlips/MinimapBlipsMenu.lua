---@diagnostic disable: undefined-global, create-global

local BLIP_SCALE          = 1.0
local BLIP_SCALE_TRACKING = 1.5

local function Icon(name) return "Interface\\AddOns\\MinimapBlips\\icons\\" .. name end

local E = Enum and Enum.MinimapBlip
local BLIP_TYPES = E and {
	{ type = E.Target,                label = TARGET,                icon = Icon("Target"),       hostileIcon = Icon("TargetHostile"), scale = BLIP_SCALE_TRACKING },
	{ type = E.Focus,                 label = FOCUS,                 icon = Icon("Focus"),        scale = BLIP_SCALE_TRACKING },
	{ type = E.Auctioneer,            label = "Auctioneer",          icon = Icon("Auctioneer"),   scale = BLIP_SCALE_TRACKING },
	{ type = E.Banker,                label = "Banker",              icon = Icon("Banker"),       scale = BLIP_SCALE_TRACKING },
	{ type = E.FlightMaster,          label = "Flight Master",       icon = Icon("FlightMaster"), scale = BLIP_SCALE_TRACKING },
	{ type = E.Innkeeper,             label = "Innkeeper",           icon = Icon("Innkeeper"),    scale = BLIP_SCALE_TRACKING },
	{ type = E.Repair,                label = "Repair",              icon = Icon("Repair"),       scale = BLIP_SCALE_TRACKING },
	{ type = E.Trainer,               label = "Trainer",             icon = Icon("Profession"),   scale = BLIP_SCALE_TRACKING },
	{ type = E.StableMaster,          label = "Stable Master",       icon = Icon("StableMaster"), scale = BLIP_SCALE_TRACKING },
	{ type = E.Battlemaster,          label = "Battlemaster",        icon = Icon("BattleMaster"), scale = BLIP_SCALE_TRACKING },
	{ type = E.Vendor,                label = "Vendor",              icon = "Interface\\Icons\\INV_Misc_Coin_02",                   scale = BLIP_SCALE },
	{ type = E.Mailbox,               label = "Mailbox",             icon = Icon("Mailbox"),                                        scale = BLIP_SCALE_TRACKING },
} or {}

local ROW_HEIGHT = 18
local ROW_WIDTH  = 140
local PADDING    = 6

local menu = CreateFrame("Frame", "MinimapBlipsMenu", UIParent)
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

local function GenerateRows()
	if menu.rows then return end

	-- "Clear All" row pinned at row index 0. No checkbox, no per-type icon —
	-- just a centered label that calls `C_Minimap.ClearAllTracking`. The DLL
	-- fires `MINIMAP_UPDATE_TRACKING` once at the end, which makes every
	-- toggle row below re-query its state.
	local clearRow = CreateFrame("Button", nil, menu)
	clearRow:SetWidth(ROW_WIDTH)
	clearRow:SetHeight(ROW_HEIGHT)
	clearRow:SetPoint("TOPLEFT", menu, "TOPLEFT", PADDING, -PADDING)
	clearRow:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
	local clearLabel = clearRow:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	clearLabel:SetPoint("CENTER", clearRow, "CENTER", 0, 0)
	clearLabel:SetText("Clear All")
	clearRow:SetScript("OnClick", function() C_Minimap.ClearAllTracking() end)

	for i, entry in ipairs(BLIP_TYPES) do
		local row = CreateFrame("Button", nil, menu)
		row:SetWidth(ROW_WIDTH)
		row:SetHeight(ROW_HEIGHT)
		-- Blip rows start at index 1 below the Clear All row at index 0.
		row:SetPoint("TOPLEFT", menu, "TOPLEFT", PADDING, -(PADDING + i * ROW_HEIGHT))
		row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")

		local check = row:CreateTexture(nil, "ARTWORK")
		check:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
		check:SetWidth(14)
		check:SetHeight(14)
		check:SetPoint("LEFT", row, "LEFT", 0, 0)
		if not C_Minimap.IsTracked(entry.type) then
			check:Hide()
		end

		local rowIcon = row:CreateTexture(nil, "ARTWORK")
		rowIcon:SetTexture(entry.icon)
		rowIcon:SetWidth(14)
		rowIcon:SetHeight(14)
		rowIcon:SetPoint("LEFT", row, "LEFT", 18, 0)
		rowIcon:SetTexCoord(0.05, 0.95, 0.05, 0.95)

		local label = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		label:SetPoint("LEFT", row, "LEFT", 36, 0)
		label:SetText(entry.label)

		row.check    = check
		row.blipType = entry.type

		row:SetScript("OnClick", function()
			C_Minimap.Toggle(row.blipType)
		end)

		row:SetScript("OnEvent", function()
			if event == "MINIMAP_UPDATE_TRACKING" then
				if C_Minimap.IsTracked(this.blipType) then
					this.check:Show()
				else
					this.check:Hide()
				end
			end
		end)
		row:RegisterEvent("MINIMAP_UPDATE_TRACKING")
	end
	menu.rows = true
end

function MinimapBlipsMenu_RegisterIcons()
	C_Minimap.RegisterIcons(BLIP_TYPES)
end

function MinimapBlipsMenu_Toggle(anchorButton)
	if menu:IsShown() then
		menu:Hide()
		return
	end
	GenerateRows()
	menu:ClearAllPoints()
	menu:SetPoint("TOPRIGHT", anchorButton, "TOPLEFT", 0, -5)
	menu:Show()
end
