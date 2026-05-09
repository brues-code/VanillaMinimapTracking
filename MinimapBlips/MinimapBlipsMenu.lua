---@diagnostic disable: undefined-global, create-global

local BLIP_SCALE          = 1.0
local BLIP_SCALE_TRACKING = 1.5

local function Icon(name) return "Interface\\AddOns\\MinimapBlips\\icons\\" .. name end

local BLIP_TYPES = {
	{ type = "target",                  label = TARGET,              icon = Icon("Target"),       hostileIcon = Icon("TargetHostile"), scale = BLIP_SCALE_TRACKING },
	{ type = "focus",                   label = FOCUS,               icon = Icon("Focus"),        scale = BLIP_SCALE_TRACKING },
	{ type = "auctioneer",              label = "Auctioneer",          icon = Icon("Auctioneer"),   scale = BLIP_SCALE_TRACKING },
	{ type = "banker",                  label = "Banker",              icon = Icon("Banker"),       scale = BLIP_SCALE_TRACKING },
	{ type = "flight master",           label = "Flight Master",       icon = Icon("FlightMaster"), scale = BLIP_SCALE_TRACKING },
	{ type = "innkeeper",               label = "Innkeeper",           icon = Icon("Innkeeper"),    scale = BLIP_SCALE_TRACKING },
	{ type = "repair",                  label = "Repair",              icon = Icon("Repair"),       scale = BLIP_SCALE_TRACKING },
	{ type = "trainer",                 label = "Trainer",             icon = Icon("Profession"),   scale = BLIP_SCALE_TRACKING },
	{ type = "stable master",           label = "Stable Master",       icon = Icon("StableMaster"), scale = BLIP_SCALE_TRACKING },
	{ type = "battlemaster",            label = "Battlemaster",        icon = Icon("BattleMaster"), scale = BLIP_SCALE_TRACKING },
	{ type = "vendor",                  label = "Vendor",              icon = "Interface\\Icons\\INV_Misc_Coin_02",                   scale = BLIP_SCALE },
	{ type = "mailbox",                 label = "Mailbox",             icon = Icon("Mailbox"),                                        scale = BLIP_SCALE_TRACKING },
	-- { type = "transmog",                label = "Transmog",            icon = Icon("Transmogrifier"),                  scale = BLIP_SCALE_TRACKING },
	-- { type = "item restore",            label = "Item Restore",        icon = "Interface\\Icons\\INV_Misc_Bag_07",                    scale = BLIP_SCALE_TRACKING },
	-- { type = "outdoor pvp",             label = "Outdoor PvP",         icon = "Interface\\Icons\\Achievement_PVP_A_01",               scale = BLIP_SCALE_TRACKING },
	-- { type = "summoning ritual unit",   label = "Ritual Unit",         icon = "Interface\\Icons\\Spell_Shadow_SummonFelhunter",       scale = BLIP_SCALE },
	-- { type = "summoning ritual object", label = "Ritual Object",       icon = "Interface\\Icons\\Spell_Shadow_SummonFelhunter",       scale = BLIP_SCALE },
	-- { type = "brainwashing",            label = "Brainwashing Device", icon = "Interface\\Icons\\spell_shadow_shadowworddominate",    scale = BLIP_SCALE },
}

local ROW_HEIGHT = 18
local ROW_WIDTH  = 140
local PADDING    = 6

local menu = CreateFrame("Frame", "MinimapBlipsMenu", UIParent)
menu:SetFrameStrata("FULLSCREEN_DIALOG")
menu:SetWidth(ROW_WIDTH + PADDING * 2)
menu:SetHeight(table.getn(BLIP_TYPES) * ROW_HEIGHT + PADDING * 2)
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

local rows = {}
for i, entry in ipairs(BLIP_TYPES) do
	local row = CreateFrame("Button", nil, menu)
	row:SetWidth(ROW_WIDTH)
	row:SetHeight(ROW_HEIGHT)
	row:SetPoint("TOPLEFT", menu, "TOPLEFT", PADDING, -(PADDING + (i - 1) * ROW_HEIGHT))
	row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")

	local check = row:CreateTexture(nil, "ARTWORK")
	check:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
	check:SetWidth(14)
	check:SetHeight(14)
	check:SetPoint("LEFT", row, "LEFT", 0, 0)

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
		C_MinimapBlip.Toggle(row.blipType)
	end)

	row:SetScript("OnEvent", function()
		if event == "MINIMAP_BLIP_TRACKING_CHANGED" then
			if this.blipType == arg1 then
				if arg2 == 1 then
					this.check:Show()
				else
					this.check:Hide()
				end
			end
		end
	end)
	row:RegisterEvent("MINIMAP_BLIP_TRACKING_CHANGED")

	rows[i] = row
end

function MinimapBlipsMenu_RegisterIcons()
	C_MinimapBlip.RegisterIcons(BLIP_TYPES)
end

function MinimapBlipsMenu_RefreshAll()
	local tracked = C_MinimapBlip.GetTracked()
	for _, row in ipairs(rows) do
		if tracked[row.blipType] then
			row.check:Show()
		else
			row.check:Hide()
		end
	end
end

function MinimapBlipsMenu_Toggle(anchorButton)
	if menu:IsShown() then
		menu:Hide()
		return
	end
	MinimapBlipsMenu_RefreshAll()
	menu:ClearAllPoints()
	menu:SetPoint("TOPRIGHT", anchorButton, "TOPLEFT", 0, -5)
	menu:Show()
end
