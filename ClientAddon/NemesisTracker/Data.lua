NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

function NT:GetZoneKey(zoneId, zoneName)
    if self.MapData and type(self.MapData.GetZoneKey) == "function" then
        local key = self.MapData:GetZoneKey(zoneId, zoneName)
        if key then return key end
    end
    return string.format("%s:%s", tostring(zoneId or 0), zoneName or "")
end

-- Visibility is entirely server-authoritative.
function NT:ShouldHideNemesis(nemesis)
    return false
end

function NT:IsGreyNemesis(nemesis)
    local npcLevel = tonumber(nemesis and nemesis.level) or 0
    local playerLevel = UnitLevel and (UnitLevel("player") or 0) or 0
    if npcLevel <= 0 or playerLevel <= 0 then return false end

    -- Keep this classification aligned with the WorldMap level colouring:
    -- six or more levels below the player is displayed as grey.
    return (npcLevel - playerLevel) <= -6
end
