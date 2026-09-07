NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

NT.UI = NT.UI or {}
local UI = NT.UI
local Astrolabe = DongleStub and DongleStub("Astrolabe-0.4")

local ICON_TEXTURE = type(NemesisTracker_Asset) == "function" and NemesisTracker_Asset("assets\\nemesis.blp") or "Interface\\AddOns\\NemesisTracker\\assets\\nemesis.blp"
local ICON_TEXTURE_RANK5 = type(NemesisTracker_Asset) == "function" and NemesisTracker_Asset("assets\\nemesis-5.blp") or "Interface\\AddOns\\NemesisTracker\\assets\\nemesis-5.blp"
local pins = {}
local activePins = 0
local BASE_ICON_ALPHA = 0.85
local HOVER_ICON_ALPHA = 1.00
local CONTINENT_WORLD_RANK5_SCALE = 0.80

-- The BLP files are 32x32 but the useful artwork occupies only the center.
-- Crop the transparent padding with UV coordinates so the configured pin
-- dimensions correspond to the actually visible icon.
local ICON_TEXCOORD = { 6 / 32, 25 / 32, 6 / 32, 25 / 32 }       -- 19x19
local ICON_TEXCOORD_RANK5 = { 5 / 32, 28 / 32, 4 / 32, 27 / 32 } -- 23x23

local function applyIconTexCoord(texture, rank)
    if not texture then return end
    local uv = (tonumber(rank) or 1) >= 5 and ICON_TEXCOORD_RANK5 or ICON_TEXCOORD
    texture:SetTexCoord(uv[1], uv[2], uv[3], uv[4])
end


local mapOptionsButton
local mapOptionsMenuFrame

local function updateMapOptionsButtonVisibility()
    if not mapOptionsButton then return end
    if WorldMapFrame and WorldMapFrame:IsShown() and WorldMapButton and WorldMapButton:IsShown() then
        mapOptionsButton:Show()
    else
        mapOptionsButton:Hide()
    end
end

local function createMapOptionsButton()
    if mapOptionsButton or not WorldMapButton then return end

    -- Keep this button outside WorldMapButton's child list and anonymous.
    -- Some map addons (notably WDM-style layouts) enumerate named controls to
    -- arrange their own buttons. NemesisTracker must never participate in that
    -- layout: it only reads existing button positions and anchors itself to the
    -- map using an independent offset.
    local mapParent = WorldMapFrame or UIParent
    mapOptionsButton = CreateFrame("Button", nil, mapParent)
    mapOptionsButton:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")
    mapOptionsButton:ClearAllPoints()
    mapOptionsButton:SetFrameStrata("TOOLTIP")
    mapOptionsButton:SetFrameLevel((WorldMapButton:GetFrameLevel() or 0) + 20)
    mapOptionsButton:SetWidth(32)
    mapOptionsButton:SetHeight(32)
    mapOptionsButton:RegisterForClicks("LeftButtonUp")

    local function isCandidate(frame, explicit)
        if not frame or frame == mapOptionsButton or frame == WorldMapButton then return false end
        if not frame.IsShown or not frame:IsShown() then return false end
        if not frame.GetObjectType or frame:GetObjectType() ~= "Button" then return false end
        if not frame.GetLeft or not frame.GetRight or not frame.GetTop then return false end

        local name = frame.GetName and frame:GetName()
        if not name then return false end

        local lowerName = string.lower(name)
        if string.find(lowerName, "nemesistrackermappin", 1, true)
            or string.find(lowerName, "poi", 1, true)
            or string.find(lowerName, "pin", 1, true)
            or string.find(lowerName, "marker", 1, true)
            or string.find(lowerName, "waypoint", 1, true)
            or string.find(lowerName, "mapnote", 1, true)
            or string.find(lowerName, "node", 1, true) then
            return false
        end

        local left, right, top = frame:GetLeft(), frame:GetRight(), frame:GetTop()
        local mapRight, mapTop = WorldMapButton:GetRight(), WorldMapButton:GetTop()
        if not left or not right or not top or not mapRight or not mapTop then return false end

        -- Menu controls live in the narrow toolbar at the top-right. Map POIs can
        -- also be Button objects, so position alone is not sufficient.
        if top < (mapTop - 48) or top > (mapTop + 16) then return false end
        if right < (mapRight - 320) or right > (mapRight + 32) then return false end

        local width = frame.GetWidth and frame:GetWidth() or 0
        local height = frame.GetHeight and frame:GetHeight() or 0
        if width < 20 or width > 48 or height < 20 or height > 48 then return false end

        if explicit then return true end

        -- Generic discovery accepts only controls whose own anchor describes a
        -- toolbar-style TOP/RIGHT placement. Normal map icons are positioned with
        -- CENTER/TOPLEFT coordinates and are therefore ignored.
        local hasToolbarAnchor = false
        local points = frame.GetNumPoints and frame:GetNumPoints() or 0
        for i = 1, points do
            local point, _, relativePoint = frame:GetPoint(i)
            point = point or ""
            relativePoint = relativePoint or ""
            if string.find(point, "RIGHT", 1, true) or string.find(point, "TOP", 1, true)
                or string.find(relativePoint, "RIGHT", 1, true) or string.find(relativePoint, "TOP", 1, true) then
                hasToolbarAnchor = true
                break
            end
        end
        if not hasToolbarAnchor then return false end

        -- A menu button must actually be interactive. This excludes decorative
        -- map overlay buttons that happen to sit in the same screen region.
        if frame.GetScript and not frame:GetScript("OnClick") then return false end

        return true
    end

    local function addCandidate(candidates, seen, frame, explicit)
        if frame and not seen[frame] and isCandidate(frame, explicit) then
            seen[frame] = true
            table.insert(candidates, frame)
        end
    end

    local function collectCandidates()
        local candidates, seen = {}, {}

        -- Known integrations first.
        addCandidate(candidates, seen, _G.RareScanner335MapOptionsButton, true)
        addCandidate(candidates, seen, _G.WDM_WorldMapButton, true)
        addCandidate(candidates, seen, _G.Questie and _G.Questie.WorldMap and _G.Questie.WorldMap.Button, true)

        -- Generic discovery of other named addon buttons already occupying the
        -- same toolbar. We only inspect; no property on these frames is changed.
        local function scanChildren(parent)
            if not parent or not parent.GetChildren then return end
            local children = { parent:GetChildren() }
            for _, child in ipairs(children) do
                addCandidate(candidates, seen, child)
            end
        end
        scanChildren(WorldMapButton)
        if WorldMapFrame and WorldMapFrame ~= WorldMapButton then
            scanChildren(WorldMapFrame)
        end

        return candidates
    end

    local function findLeftmostButton()
        local best, bestLeft
        for _, frame in ipairs(collectCandidates()) do
            local left = frame:GetLeft()
            if left and (not bestLeft or left < bestLeft) then
                best, bestLeft = frame, left
            end
        end
        return best, bestLeft
    end

    local function updatePosition()
        if not mapOptionsButton or not WorldMapButton then return end
        local mapRight, mapTop = WorldMapButton:GetRight(), WorldMapButton:GetTop()
        if not mapRight or not mapTop then return end

        local anchor, anchorLeft = findLeftmostButton()
        local xOffset, yOffset = -4, -4

        if anchor and anchorLeft then
            -- Compute the desired position from screen coordinates, then anchor
            -- only to WorldMapButton. We deliberately do NOT SetPoint to WDM,
            -- Questie or RareScanner, so there is no reciprocal anchor chain.
            xOffset = anchorLeft - mapRight - 2
            local anchorTop = anchor:GetTop()
            if anchorTop then
                yOffset = anchorTop - mapTop
            end
        end

        mapOptionsButton:ClearAllPoints()
        mapOptionsButton:SetPoint("TOPRIGHT", WorldMapButton, "TOPRIGHT", xOffset, yOffset)
    end

    local function updateLayout()
        if not mapOptionsButton or not WorldMapButton then return end
        mapOptionsButton:SetScale(1)
        updatePosition()
        updateMapOptionsButtonVisibility()
    end

    local background = mapOptionsButton:CreateTexture(nil, "BACKGROUND")
    background:SetWidth(25)
    background:SetHeight(25)
    background:SetPoint("TOPLEFT", 2, -4)
    background:SetTexture("Interface\\Minimap\\UI-Minimap-Background")

    local icon = mapOptionsButton:CreateTexture(nil, "ARTWORK")
    icon:SetWidth(20)
    icon:SetHeight(20)
    icon:SetPoint("TOPLEFT", 6, -5)
    icon:SetTexture(ICON_TEXTURE)
    applyIconTexCoord(icon, 1)

    local border = mapOptionsButton:CreateTexture(nil, "OVERLAY")
    border:SetWidth(54)
    border:SetHeight(54)
    border:SetPoint("TOPLEFT")
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")

    local menu = {
        { text = "NemesisTracker", isTitle = true },
        {
            text = "Afficher les Némésis",
            keepShownOnClick = 1,
            checked = function()
                return not (NT.db and NT.db.showOnMap == false)
            end,
            func = function()
                NT.db = NT.db or {}
                NT.db.showOnMap = not (NT.db.showOnMap ~= false)
                UI:RefreshMap()
            end,
        },
        {
            text = "Masquer les Némésis bas niveau",
            keepShownOnClick = 1,
            checked = function()
                return not (NT.db and NT.db.hideLowLevelNemeses == false)
            end,
            func = function()
                NT.db = NT.db or {}
                NT.db.hideLowLevelNemeses = not (NT.db.hideLowLevelNemeses ~= false)
                UI:RefreshMap()
            end,
        },
    }

    mapOptionsButton:SetScript("OnClick", function(self, button)
        if button ~= "LeftButton" then return end
        if not mapOptionsMenuFrame then
            mapOptionsMenuFrame = CreateFrame("Frame", "NemesisTrackerMapOptionsMenu", UIParent, "UIDropDownMenuTemplate")
        end
        EasyMenu(menu, mapOptionsMenuFrame, self, 0, 0, "MENU", 0)
    end)

    updateLayout()

    if not mapOptionsButton.ntLayoutHooked then
        mapOptionsButton.ntLayoutHooked = true
        WorldMapButton:HookScript("OnShow", updateLayout)
        WorldMapButton:HookScript("OnSizeChanged", updateLayout)
        if WorldMapFrame then
            WorldMapFrame:HookScript("OnShow", updateLayout)
            WorldMapFrame:HookScript("OnHide", updateMapOptionsButtonVisibility)
            WorldMapFrame:HookScript("OnSizeChanged", updateLayout)
        end
        if type(WorldMapFrame_Update) == "function" then
            hooksecurefunc("WorldMapFrame_Update", updateLayout)
        end
    end
end

local RANK_LABELS = {
    [1] = { text = "Rang I — Traqué", color = "ffb0b0b0" },
    [2] = { text = "Rang II — Dangereux", color = "ff40ff40" },
    [3] = { text = "Rang III — Redoutable", color = "ff4080ff" },
    [4] = { text = "Rang IV — Fléau", color = "ffa040ff" },
    [5] = { text = "Rang V - Légendaire", color = "ffff8000" },
}

local INSTANCE_MAPS = {
  [686] = {
    name = "ZulFarrak",
    mapID = 209,
    floors = {
      { 1383.33322143555, 922.916625976562, -1624.99987792969, 2052.08325195312, -241.666656494141, 1129.16662597656 },
    },
  },
  [691] = {
    name = "Gnomeregan",
    mapID = 90,
    floors = {
      { 769.667999267578, 513.111999511719, 277.772003173828, -694.0, -491.89599609375, -180.888000488281 },
      { 769.667999267578, 513.111999511719, 77.7720031738281, -714.0, -691.89599609375, -200.888000488281 },
      { 869.667999267578, 579.778015136719, 127.772003173828, -967.3330078125, -741.89599609375, -387.554992675781 },
      { 869.669708251953, 579.779998779297, -72.9992980957031, -937.333984375, -942.669006347656, -357.553985595703 },
    },
  },
  [699] = {
    name = "DireMaul",
    mapID = 429,
    floors = {
      { 1275.0, 850.0, 387.5, 200.0, -887.5, 1050.0 },
      { 525.0, 350.0, -125.0, -150.0, -650.0, 200.0 },
      { 487.5, 325.0, -231.25, -150.0, -718.75, 175.0 },
      { 750.0, 500.0, -325.0, -250.0, -1075.0, 250.0 },
      { 800.000801086426, 533.333999633789, 900.0, -281.6669921875, 99.9991989135742, 251.667007446289 },
      { 975.0, 650.0, 862.5, -200.0, -112.5, 450.0 },
    },
  },
  [704] = {
    name = "BlackrockDepths",
    mapID = 230,
    floors = {
      { 1407.06097412109, 938.040756225586, 884.723999023438, 248.639663696289, -522.336975097656, 1186.68041992188 },
      { 1507.06097412109, 1004.70742797852, 934.723999023438, 495.302825927734, -572.336975097656, 1500.01025390625 },
    },
  },
  [721] = {
    name = "BlackrockSpire",
    mapID = 229,
    floors = {
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
      { 886.839014053345, 591.226013183594, 876.252014160156, -286.828002929688, -10.5869998931885, 304.398010253906 },
    },
  },
  [749] = {
    name = "WailingCaverns",
    mapID = 43,
    floors = {
      { 936.475006103516, 624.315994262695, 375.946014404297, -410.14599609375, -560.528991699219, 214.169998168945 },
    },
  },
  [750] = {
    name = "Maraudon",
    mapID = 349,
    floors = {
      { 975.0, 650.0, 827.5, 550.0, -147.5, 1200.0 },
      { 1637.5, 1091.66600036621, 1158.75, -208.332992553711, -478.75, 883.3330078125 },
    },
  },
  [756] = {
    name = "TheDeadmines",
    mapID = 36,
    floors = {
      { 559.264007568359, 372.842502593994, 796.622009277344, -337.509002685547, 237.358001708984, 35.3334999084473 },
      { 499.263000488281, 332.842300415039, 1016.61999511719, -267.509002685547, 517.356994628906, 65.3332977294922 },
    },
  },
  [761] = {
    name = "RazorfenKraul",
    mapID = 47,
    floors = {
      { 736.449951171875, 490.959838867188, -1322.46997070312, 1858.68005371094, -2058.919921875, 2349.63989257812 },
    },
  },
  [762] = {
    name = "ScarletMonastery",
    mapID = 189,
    floors = {
      { 619.983947753906, 413.32275390625, -947.986022949219, 1616.85864257812, -1567.96997070312, 2030.18139648438 },
      { 320.190994262695, 213.460494995117, 482.463989257812, 93.9055023193359, 162.272994995117, 307.365997314453 },
      { 612.69660949707, 408.4599609375, 562.424011230469, 1600.64001464844, -50.2725982666016, 2009.09997558594 },
      { 703.300048828125, 468.86669921875, -1040.68994140625, 812.423706054688, -1743.98999023438, 1281.29040527344 },
    },
  },
  [764] = {
    name = "ShadowfangKeep",
    mapID = 33,
    floors = {
      { 352.429931640625, 234.953392028809, -2003.77001953125, -319.882995605469, -2356.19995117188, -84.9296035766602 },
      { 212.419921875, 141.61799621582, -2147.56005859375, -303.214996337891, -2359.97998046875, -161.59700012207 },
      { 152.429931640625, 101.619903564453, -2103.77001953125, -193.216003417969, -2256.19995117188, -91.5960998535156 },
      { 152.429931640625, 101.624694824219, -2103.77001953125, -193.214996337891, -2256.19995117188, -91.5903015136719 },
      { 152.429931640625, 101.624694824219, -2103.77001953125, -193.214996337891, -2256.19995117188, -91.5903015136719 },
      { 198.429931640625, 132.286605834961, -2080.77001953125, -182.546005249023, -2279.19995117188, -50.2593994140625 },
      { 272.429931640625, 181.619903564453, -2023.77001953125, -278.216003417969, -2296.19995117188, -96.5960998535156 },
    },
  },
  [765] = {
    name = "Stratholme",
    mapID = 329,
    floors = {
      { 705.719970703125, 470.47998046875, 3617.67993164062, 3338.96997070312, 2911.9599609375, 3809.44995117188 },
      { 1005.72045898438, 670.480224609375, 3967.68017578125, 3498.96997070312, 2961.95971679688, 4169.4501953125 },
    },
  },
}


local function normalizeInstanceMapName(mapName)
    if not mapName then return nil end
    return string.match(mapName, "^(.-)%d*_$") or mapName
end

local function getDisplayedInstanceMap()
    local instance
    if type(GetMapInfo) == "function" then
        local mapName = normalizeInstanceMapName(GetMapInfo())
        if mapName then
            for _, candidate in pairs(INSTANCE_MAPS) do
                if candidate.name == mapName then
                    instance = candidate
                    break
                end
            end
        end
    end

    if not instance then return nil end

    local floor = 1
    if type(GetCurrentMapDungeonLevel) == "function" then
        local currentFloor = GetCurrentMapDungeonLevel()
        if currentFloor and currentFloor > 0 then
            floor = currentFloor
        end
    end
    if floor > #instance.floors then
        floor = #instance.floors
    end

    return instance, instance.floors[floor], floor
end

local function instanceWorldToMap(floorData, spawnX, spawnY)
    if not floorData then return nil end
    local width, height = floorData[1], floorData[2]
    local lowerRightX, lowerRightY = floorData[5], floorData[6]
    if not width or not height or width == 0 or height == 0 then return nil end

    local left = -lowerRightX
    local top = lowerRightY
    local mapX = (left - spawnY) / width
    local mapY = (top - spawnX) / height
    if mapX < 0 or mapX > 1 or mapY < 0 or mapY > 1 then return nil end
    return mapX, mapY
end

local function hidePins()
    for index = 1, #pins do
        pins[index]:Hide()
    end
    activePins = 0
end


local function getPin(index)
    local pin = pins[index]
    if pin then return pin end

    pin = CreateFrame("Button", "NemesisTrackerMapPin" .. index, WorldMapButton)
    pin:EnableMouse(true)
    pin:SetFrameStrata(WorldMapButton:GetFrameStrata())
    pin:SetFrameLevel((WorldMapButton:GetFrameLevel() or 0) + 20)
    pin:SetHitRectInsets(-3, -3, -3, -3)

    local texture = pin:CreateTexture(nil, "OVERLAY")
    texture:SetAllPoints(pin)
    texture:SetTexture(ICON_TEXTURE)
    applyIconTexCoord(texture, 1)
    pin.texture = texture

    local highlight = pin:CreateTexture(nil, "HIGHLIGHT")
    highlight:SetAllPoints(pin)
    highlight:SetTexture(ICON_TEXTURE)
    applyIconTexCoord(highlight, 1)
    highlight:SetBlendMode("ADD")
    highlight:SetAlpha(0.6)
    pin.highlight = highlight

    -- Match the WorldMap tooltip path used by RareScanner on 3.3.5a.
    -- Blizzard's WorldMapPOI handlers consume self.name/self.description and
    -- take care of tooltip ownership/placement for map POIs.
    if type(WorldMapPOI_OnEnter) == "function" then
        pin:SetScript("OnEnter", function(self)
            if self.texture then self.texture:SetAlpha(HOVER_ICON_ALPHA) end
            if self.name then
                WorldMapPOI_OnEnter(self)
            end
        end)
    else
        pin:SetScript("OnEnter", function(self)
            if self.texture then self.texture:SetAlpha(HOVER_ICON_ALPHA) end
            if not self.name or not GameTooltip then return end
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(self.name, 1, 1, 1)
            GameTooltip:Show()
        end)
    end

    if type(WorldMapPOI_OnLeave) == "function" then
        pin:SetScript("OnLeave", function(self)
            if self.texture then self.texture:SetAlpha(BASE_ICON_ALPHA) end
            WorldMapPOI_OnLeave(self)
        end)
    else
        pin:SetScript("OnLeave", function(self)
            if self.texture then self.texture:SetAlpha(BASE_ICON_ALPHA) end
            if GameTooltip then GameTooltip:Hide() end
        end)
    end

    pins[index] = pin
    return pin
end

local function buildPinName(nemesis)
    local npcLevel = tonumber(nemesis and nemesis.level) or 0
    local playerLevel = UnitLevel("player") or 1
    local r, g, b = 0.65, 0.65, 0.65

    if npcLevel > 0 then
        local diff = npcLevel - playerLevel
        if diff >= 3 then
            r, g, b = 1.0, 0.1, 0.1
        elseif diff >= -2 then
            r, g, b = 1.0, 0.5, 0.0
        elseif diff >= -5 then
            r, g, b = 0.25, 1.0, 0.25
        end
    end

    local levelColor = string.format("|cff%02x%02x%02x", math.floor(r * 255), math.floor(g * 255), math.floor(b * 255))
    local rank = math.max(1, math.min(5, tonumber(nemesis and nemesis.rank) or 1))
    local rankInfo = RANK_LABELS[rank] or RANK_LABELS[1]
    local serverName = (nemesis and (nemesis.serverName or nemesis.name)) or "Nemesis"

    return string.format(
        "%s %s[%d]|r\n|c%s%s|r",
        serverName,
        levelColor,
        npcLevel,
        rankInfo.color,
        rankInfo.text
    )
end

local function addPin(x, y, nemesis, mapScale)
    if not WorldMapButton or not x or not y then return end
    if x <= 0 or x >= 1 or y <= 0 or y >= 1 then return end

    activePins = activePins + 1
    local pin = getPin(activePins)
    local rank = math.max(1, math.min(5, tonumber(nemesis.rank) or 1))
    local textureScale = rank >= 5 and 0.70 or 0.60
    local size = (16 + ((rank - 1) * 2)) * textureScale * (mapScale or 1.0)

    pin:ClearAllPoints()
    pin:SetWidth(size)
    pin:SetHeight(size)
    pin:SetPoint("CENTER", WorldMapButton, "TOPLEFT", x * WorldMapButton:GetWidth(), -y * WorldMapButton:GetHeight())
    local iconTexture = ((nemesis.rank or 1) >= 5) and ICON_TEXTURE_RANK5 or ICON_TEXTURE
    pin.texture:SetTexture(iconTexture)
    applyIconTexCoord(pin.texture, rank)
    pin.texture:SetAlpha(BASE_ICON_ALPHA)
    if pin.highlight then
        pin.highlight:SetTexture(iconTexture)
        applyIconTexCoord(pin.highlight, rank)
    end
    pin.nemesis = nemesis
    pin.name = buildPinName(nemesis)
    pin.description = nil
    pin:Show()
end

-- Server world-map coordinates projected directly into Astrolabe continent
-- coordinates. Bounds come from the 3.3.5 WorldMapArea continent records.
-- The network contract therefore needs only MapID + world X/Y.
local WORLD_MAP_BOUNDS = {
    [0]   = { continent = 2, left = 18171.97,  right = -22569.21, top = 11176.34, bottom = -15973.34 }, -- Eastern Kingdoms
    [1]   = { continent = 1, left = 17066.60,  right = -19733.21, top = 12799.90, bottom = -11733.30 }, -- Kalimdor
    [530] = { continent = 3, left = 12996.04,  right = -4468.039, top = 5821.359, bottom = -5821.359 }, -- Outland
    [571] = { continent = 4, left = 9217.152,  right = -8534.246, top = 10593.38, bottom = -1240.89 }, -- Northrend
}

local function worldPositionToAstrolabe(mapId, worldX, worldY)
    local bounds = WORLD_MAP_BOUNDS[tonumber(mapId)]
    worldX, worldY = tonumber(worldX), tonumber(worldY)
    if not bounds or not worldX or not worldY then return nil end

    local width = bounds.right - bounds.left
    local height = bounds.bottom - bounds.top
    if width == 0 or height == 0 then return nil end

    local x = (worldY - bounds.left) / width
    local y = (worldX - bounds.top) / height
    return bounds.continent, x, y
end

local function shouldDisplayNemesisOnMap(nemesis)
    if NT:ShouldHideNemesis(nemesis) then return false end
    if NT.db and NT.db.hideLowLevelNemeses ~= false and NT.IsGreyNemesis and NT:IsGreyNemesis(nemesis) then
        return false
    end
    return true
end

local function findAstrolabeZoneByFile(continent, mapFile)
    if not Astrolabe or not Astrolabe.ContinentList or not mapFile then return nil end
    local zones = Astrolabe.ContinentList[continent]
    if not zones then return nil end
    for zoneIndex, fileName in pairs(zones) do
        if fileName == mapFile then return zoneIndex end
    end
    return nil
end

function UI:RefreshMap()
    hidePins()
    if not WorldMapButton or not WorldMapButton:IsShown() then return end
    if NT.db and NT.db.showOnMap == false then return end

    local instance, bounds = getDisplayedInstanceMap()
    if instance then
        for _, nemesis in pairs(NT.data.nemeses or {}) do
            if shouldDisplayNemesisOnMap(nemesis) and nemesis.mapId == instance.mapID then
                local x, y = instanceWorldToMap(bounds, nemesis.x or 0, nemesis.y or 0)
                if x and y then addPin(x, y, nemesis, 1.0) end
            end
        end
        return
    end

    if not Astrolabe then return end

    local currentContinent = GetCurrentMapContinent and GetCurrentMapContinent() or 0
    local currentZone = GetCurrentMapZone and GetCurrentMapZone() or 0
    local currentMapFile = type(GetMapInfo) == "function" and GetMapInfo() or nil

    -- WDM microdungeons are not normal entries returned by GetMapZones().
    -- RareScanner/WDM solve this by keeping their geometry in Astrolabe's
    -- WorldMapSize data. Detect the currently displayed custom map by its
    -- GetMapInfo() map-file name and let Astrolabe resolve the owning
    -- continent. This requires no zoneID/areaID from Nemesis data.
    local customMapContinent
    if currentMapFile and type(Astrolabe.GetCustomMapFileContinent) == "function" then
        customMapContinent = Astrolabe:GetCustomMapFileContinent(currentMapFile)
    end

    if currentContinent == nil then currentContinent = 0 end
    if currentContinent < 0 and not customMapContinent then return end

    local effectiveContinent = customMapContinent or currentContinent
    local standardTargetZone = (not customMapContinent and effectiveContinent > 0)
        and findAstrolabeZoneByFile(effectiveContinent, currentMapFile) or nil
    local isWorldMap = not customMapContinent and currentContinent == 0
    local isContinentMap = not customMapContinent and currentContinent > 0 and currentZone == 0
    local isCustomRegionMap = customMapContinent ~= nil
    local isRegionMap = standardTargetZone ~= nil or isCustomRegionMap

    for _, nemesis in pairs(NT.data.nemeses or {}) do
        if shouldDisplayNemesisOnMap(nemesis) then
            local rank = tonumber(nemesis.rank) or 1

            -- N1-N4 are intentionally restricted to region/sub-zone maps.
            -- N5 remain visible when zooming out to the continent and world.
            if isRegionMap or rank >= 5 then
                local sourceContinent, sourceX, sourceY = worldPositionToAstrolabe(nemesis.mapId, nemesis.x, nemesis.y)
                if sourceContinent and sourceX and sourceY then
                    local x, y

                    if isRegionMap then
                        if sourceContinent == effectiveContinent then
                            if standardTargetZone then
                                x, y = Astrolabe:TranslateWorldMapPosition(
                                    sourceContinent, 0, sourceX, sourceY,
                                    effectiveContinent, standardTargetZone
                                )
                            elseif isCustomRegionMap and type(Astrolabe.TranslateWorldMapPositionToMapFile) == "function" then
                                x, y = Astrolabe:TranslateWorldMapPositionToMapFile(
                                    sourceContinent, 0, sourceX, sourceY,
                                    effectiveContinent, currentMapFile
                                )
                            end
                        end
                    elseif isContinentMap then
                        if sourceContinent == currentContinent then
                            x, y = sourceX, sourceY
                        end
                    elseif isWorldMap then
                        -- Outland has its own Astrolabe parent world.
                        if sourceContinent ~= 3 then
                            x, y = Astrolabe:TranslateWorldMapPosition(
                                sourceContinent, 0, sourceX, sourceY, 0, 0
                            )
                        end
                    end

                    if x and y and x >= 0 and x <= 1 and y >= 0 and y <= 1 then
                        local mapScale = 1.0
                        if rank >= 5 and (isContinentMap or isWorldMap) then
                            mapScale = CONTINENT_WORLD_RANK5_SCALE
                        end
                        addPin(x, y, nemesis, mapScale)
                    end
                end
            end
        end
    end

end

function UI:Create()
    if self.created then return end
    self.created = true

    createMapOptionsButton()

    if type(WorldMapFrame_Update) == "function" then
        hooksecurefunc("WorldMapFrame_Update", function()
            UI:RefreshMap()
        end)
    end
    if WorldMapButton then
        WorldMapButton:HookScript("OnShow", function() UI:RefreshMap() end)
        WorldMapButton:HookScript("OnHide", hidePins)
        WorldMapButton:HookScript("OnSizeChanged", function() UI:RefreshMap() end)
    end
end

function UI:RefreshAll() self:RefreshMap() end
