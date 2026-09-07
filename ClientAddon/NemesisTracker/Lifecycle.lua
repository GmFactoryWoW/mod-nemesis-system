NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

local function applyDefaults(target, defaults)
    for key, value in pairs(defaults) do
        if type(value) == "table" then
            if type(target[key]) ~= "table" then target[key] = {} end
            applyDefaults(target[key], value)
        elseif target[key] == nil then
            target[key] = value
        end
    end
end

function NT:InitializeDatabase()
    local defaults = {
        showOnMap = true,
        hideLowLevelNemeses = true,
        cache = { protocolVersion = NT.protocolVersion or 4, nemeses = {} },
    }

    NemesisTrackerDB = NemesisTrackerDB or {}
    if type(NemesisTrackerDB.profile) ~= "table" then
        local migratedProfile
        if type(NemesisTrackerDB.profiles) == "table" then
            for _, candidate in pairs(NemesisTrackerDB.profiles) do
                if type(candidate) == "table" then
                    migratedProfile = candidate
                    if candidate.cache and candidate.cache.nemeses then break end
                end
            end
        end
        NemesisTrackerDB.profile = migratedProfile or {}
    end
    applyDefaults(NemesisTrackerDB.profile, defaults)
    self.database = NemesisTrackerDB
    self.db = NemesisTrackerDB.profile
    self.db.cache = self.db.cache or { protocolVersion = NT.protocolVersion or 4, nemeses = {} }
    if tonumber(self.db.cache.protocolVersion) ~= tonumber(NT.protocolVersion or 4) then
        self.db.cache = { protocolVersion = NT.protocolVersion or 4, nemeses = {} }
    end
    self.db.cache.protocolVersion = NT.protocolVersion or 4
    self.db.cache.nemeses = self.db.cache.nemeses or {}
    self.data.nemeses = self.db.cache.nemeses
    wipe(self.data.nemesesByUnitGuid)
    for _, nemesis in pairs(self.data.nemeses) do
        if type(nemesis.unitGuid) == "string" then
            local guid = string.upper(nemesis.unitGuid)
            if string.match(guid, "^0X[%x]+$") then
                self.data.nemesesByUnitGuid[guid] = nemesis
            end
        end
    end

end

function NT:SlashCommand(input)
    input = string.lower(input or "")
    if input == "map" or input == "" then
        if ToggleWorldMap then ToggleWorldMap() end
    end
    if self.UI then self.UI:RefreshMap() end
end

function NT:RefreshVisibleUI()
    if self.UI then self.UI:RefreshMap() end
end

function NT:OnInitialize()
    self:InitializeDatabase()
    if self.UI then
        self.UI:Create()
        self.UI:RefreshAll()
    end

    if self.UnitUI then
        self.UnitUI:Initialize()
    end

    SLASH_NEMESISTRACKER1 = "/nemesistracker"
    SLASH_NEMESISTRACKER2 = "/ntrack"
    SlashCmdList.NEMESISTRACKER = function(msg) NT:SlashCommand(msg) end

end

function NT:OnEnable()
    self:RegisterEvent("CHAT_MSG_ADDON")
    self:SendAddonHello()
    self.refreshTimer = self:ScheduleRepeatingTimer("RefreshVisibleUI", 1)
end

function NT:CHAT_MSG_ADDON(prefix, message, channel, sender)
    self:HandleAddonMessage(prefix, message, channel, sender)
end


local function initializeRuntime()
    if NT.runtimeInitialized then return end
    NT.runtimeInitialized = true
    NT:OnInitialize()
    NT:OnEnable()
end

function NT:ADDON_LOADED(loadedAddon)
    if self.isFrameXML or loadedAddon ~= self.addonName then return end
    self:UnregisterEvent("ADDON_LOADED")
    initializeRuntime()
end

function NT:VARIABLES_LOADED()
    if not self.isFrameXML then return end
    self:UnregisterEvent("VARIABLES_LOADED")
    initializeRuntime()
end

NT.eventFrame:SetScript("OnEvent", function(_, event, ...)
    local handler = NT[event]
    if type(handler) == "function" then
        handler(NT, ...)
    end
end)

if NT.isFrameXML then
    NT:RegisterEvent("VARIABLES_LOADED")
else
    NT:RegisterEvent("ADDON_LOADED")
end
