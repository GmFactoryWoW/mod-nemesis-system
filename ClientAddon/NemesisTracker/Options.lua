NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

local optionsPanel

local function refreshMap()
    if NT.UI and type(NT.UI.RefreshMap) == "function" then
        NT.UI:RefreshMap()
    end
end

local function getShowOnMap()
    return not (NT.db and NT.db.showOnMap == false)
end

local function getHideLowLevel()
    return not (NT.db and NT.db.hideLowLevelNemeses == false)
end

function NT:RefreshOptionsPanel()
    if not optionsPanel then return end
    if optionsPanel.showOnMap then
        optionsPanel.showOnMap:SetChecked(getShowOnMap())
    end
    if optionsPanel.hideLowLevel then
        optionsPanel.hideLowLevel:SetChecked(getHideLowLevel())
    end
end

function NT:BuildOptionsPanel()
    if optionsPanel then return optionsPanel end
    if type(InterfaceOptions_AddCategory) ~= "function" then return nil end

    local panel = CreateFrame("Frame", "NemesisTrackerOptionsPanel")
    panel.name = "NemesisTracker"

    local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 16, -16)
    title:SetText("NemesisTracker")

    local showOnMap = CreateFrame("CheckButton", "NemesisTrackerOptShowOnMap", panel, "InterfaceOptionsCheckButtonTemplate")
    showOnMap:SetPoint("TOPLEFT", title, "BOTTOMLEFT", -2, -18)
    _G["NemesisTrackerOptShowOnMapText"]:SetText("Afficher les Nemesis sur la carte du monde")
    showOnMap.tooltipText = "Affiche les icones des Nemesis communiquees par le serveur sur les cartes compatibles."
    showOnMap:SetScript("OnClick", function(self)
        NT.db = NT.db or {}
        NT.db.showOnMap = self:GetChecked() and true or false
        refreshMap()
        NT:RefreshOptionsPanel()
    end)

    local hideLowLevel = CreateFrame("CheckButton", "NemesisTrackerOptHideLowLevel", panel, "InterfaceOptionsCheckButtonTemplate")
    hideLowLevel:SetPoint("TOPLEFT", showOnMap, "BOTTOMLEFT", 0, -8)
    _G["NemesisTrackerOptHideLowLevelText"]:SetText("Masquer les Nemesis de bas niveau")
    hideLowLevel.tooltipText = "Masque les Nemesis consideres gris pour le niveau actuel du personnage."
    hideLowLevel:SetScript("OnClick", function(self)
        NT.db = NT.db or {}
        NT.db.hideLowLevelNemeses = self:GetChecked() and true or false
        refreshMap()
        NT:RefreshOptionsPanel()
    end)

    panel.showOnMap = showOnMap
    panel.hideLowLevel = hideLowLevel
    panel.refresh = function() NT:RefreshOptionsPanel() end
    panel:SetScript("OnShow", panel.refresh)
    panel.okay = function() end
    panel.cancel = function() NT:RefreshOptionsPanel() end
    panel.default = function()
        NT.db = NT.db or {}
        NT.db.showOnMap = true
        NT.db.hideLowLevelNemeses = true
        NT:RefreshOptionsPanel()
        refreshMap()
    end

    InterfaceOptions_AddCategory(panel)
    optionsPanel = panel
    NT.optionsPanel = panel
    NT:RefreshOptionsPanel()
    return panel
end

-- Register the Interface > AddOns category at file load time so the panel is
-- available both from a normal addon TOC and when the XML is loaded by FrameXML.
NT:BuildOptionsPanel()
