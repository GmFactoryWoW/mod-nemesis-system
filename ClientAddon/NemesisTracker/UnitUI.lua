NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

NT.UnitUI = NT.UnitUI or {}
local UnitUI = NT.UnitUI

local PORTRAIT_TEXTURE = type(NemesisTracker_Asset) == "function"
    and NemesisTracker_Asset("assets\\nemesis-portrait.blp")
    or "Interface\\AddOns\\NemesisTracker\\assets\\nemesis-portrait.blp"


local PORTRAIT_TEXCOORDS = {
    [1] = { 0.00, 0.20, 0.00, 1.00 },
    [2] = { 0.20, 0.40, 0.00, 1.00 },
    [3] = { 0.40, 0.60, 0.00, 1.00 },
    [4] = { 0.60, 0.80, 0.00, 1.00 },
    [5] = { 0.80, 1.00, 0.00, 1.00 },
}

local function setPortraitRankTexture(texture, rank)
    if not texture then return end
    rank = math.max(1, math.min(5, tonumber(rank) or 1))
    local uv = PORTRAIT_TEXCOORDS[rank]
    texture:SetTexCoord(uv[1], uv[2], uv[3], uv[4])
end

local RANK_LABELS = {
    [1] = { text = "Rang I — Traqué", color = "ffb0b0b0" },
    [2] = { text = "Rang II — Dangereux", color = "ff40ff40" },
    [3] = { text = "Rang III — Redoutable", color = "ff4080ff" },
    [4] = { text = "Rang IV — Fléau", color = "ffa040ff" },
    [5] = { text = "Rang V - Légendaire", color = "ffff8000" },
}

local function getRankText(rank)
    rank = math.max(1, math.min(5, tonumber(rank) or 1))
    local info = RANK_LABELS[rank]
    return "|c" .. info.color .. info.text .. "|r"
end

local function normalizeGuid(guid)
    if type(guid) ~= "string" then return nil end
    guid = string.upper(guid)
    if not string.match(guid, "^0X[%x]+$") then return nil end
    return guid
end

local function decodeCreatureGuid(guid)
    guid = normalizeGuid(guid)
    if not guid then return nil, nil end

    local hex = string.sub(guid, 3)
    if #hex < 16 then
        hex = string.rep("0", 16 - #hex) .. hex
    elseif #hex > 16 then
        hex = string.sub(hex, -16)
    end

    -- 3.3.5 creature GUID layout: HHHH EEEEEE SSSSSS
    local entry = tonumber(string.sub(hex, 5, 10), 16)
    local spawnId = tonumber(string.sub(hex, 11, 16), 16)
    return entry, spawnId
end

function NT:GetNemesisForUnit(unit)
    if not unit or not UnitExists(unit) or UnitIsPlayer(unit) then return nil end
    if not self.data then return nil end

    local guid = normalizeGuid(UnitGUID(unit))
    if not guid then return nil end

    if self.data.nemesesByUnitGuid then
        local exact = self.data.nemesesByUnitGuid[guid]
        if exact then return exact end
    end

    -- Guard against representation differences in the full 64-bit GUID string.
    -- The low 24-bit spawn counter and 24-bit creature entry are exact in the
    -- 3.3.5 Unit GUID and are safe to decode as Lua numbers.
    local entry, spawnId = decodeCreatureGuid(guid)
    local candidate = spawnId and self.data.nemeses and self.data.nemeses[spawnId] or nil
    if candidate and (tonumber(candidate.creatureEntry) or 0) == (entry or -1) then
        return candidate
    end
    return nil
end

function UnitUI:UpdateTargetPortrait()
    if not self.portraitIcon then return end
    local nemesis = NT:GetNemesisForUnit("target")
    if nemesis then
        setPortraitRankTexture(self.portraitTexture, nemesis.rank)
        self.portraitIcon:Show()
    else
        self.portraitIcon:Hide()
    end
end

function UnitUI:AddRankToTooltip(tooltip, unit)
    if not tooltip or not unit then return end
    local nemesis = NT:GetNemesisForUnit(unit)
    if not nemesis then return end

    local guid = normalizeGuid(UnitGUID(unit))
    if tooltip.__nemesisRankGuid == guid then return end
    tooltip.__nemesisRankGuid = guid
    tooltip:AddLine(getRankText(nemesis.rank))
    tooltip:Show()
end

function UnitUI:Initialize()
    if self.initialized then return end
    self.initialized = true

    if GameTooltip then
        if GameTooltip.HookScript then
            GameTooltip:HookScript("OnTooltipSetUnit", function(tooltip)
                local _, unit = tooltip:GetUnit()
                if unit then UnitUI:AddRankToTooltip(tooltip, unit) end
            end)
            GameTooltip:HookScript("OnTooltipCleared", function(tooltip)
                tooltip.__nemesisRankGuid = nil
            end)
        end

        -- Some 3.3.5 clients/addons do not dispatch OnTooltipSetUnit reliably.
        -- Hook SetUnit as a second, guarded path.
        if type(hooksecurefunc) == "function" then
            pcall(function()
                hooksecurefunc(GameTooltip, "SetUnit", function(tooltip, unit)
                    UnitUI:AddRankToTooltip(tooltip, unit)
                end)
            end)
        end
    end

    if TargetFrame then
        -- Keep the Nemesis overlay outside TargetFrame's protected hierarchy.
        -- It is visually anchored to TargetFrame but parented to UIParent so
        -- live UPSERT/REMOVE refreshes can safely show/hide it during combat.
        local iconFrame = CreateFrame("Frame", nil, UIParent)
        -- The portrait asset is a 256x128 sprite sheet containing five
        -- vertical banners from N1 (left) to N5 (right). 24x60 keeps the
        -- individual ~51x128 source slice close to its original aspect ratio.
        iconFrame:SetWidth(24)
        iconFrame:SetHeight(60)
        iconFrame:SetFrameStrata(TargetFrame:GetFrameStrata())
        iconFrame:SetFrameLevel((TargetFrame:GetFrameLevel() or 0) + 20)

        local levelText = _G.TargetFrameTextureFrameLevelText
        if levelText then
            iconFrame:SetPoint("RIGHT", levelText, "LEFT", -155, 25)
        else
            iconFrame:SetPoint("TOPRIGHT", TargetFrame, "TOPRIGHT", -62, -35)
        end

        local icon = iconFrame:CreateTexture(nil, "OVERLAY")
        icon:SetAllPoints(iconFrame)
        icon:SetTexture(PORTRAIT_TEXTURE)
        setPortraitRankTexture(icon, 1)
        iconFrame:Hide()

        self.portraitIcon = iconFrame
        self.portraitTexture = icon
    end

    NT:RegisterEvent("PLAYER_TARGET_CHANGED")
    self:UpdateTargetPortrait()
end

function NT:PLAYER_TARGET_CHANGED()
    if self.UnitUI then self.UnitUI:UpdateTargetPortrait() end
end
