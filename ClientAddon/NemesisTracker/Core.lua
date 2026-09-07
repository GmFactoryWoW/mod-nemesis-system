local addonName = ...

NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

NT.addonName = "NemesisTracker"
NT.isFrameXML = NemesisTracker_IS_FRAME_XML == true
NT.MapData = NT.MapData or {}
NT.UI = NT.UI or {}

NT.prefix = "Nemesis"
NT.protocolVersion = 5
NT.db = NT.db or {}
NT.data = NT.data or {
    nemeses = {},
    nemesesByUnitGuid = {},
    chunks = {},
    bootstrapActive = false,
}
NT.data.nemesesByUnitGuid = NT.data.nemesesByUnitGuid or {}

NT.eventFrame = NT.eventFrame or CreateFrame("Frame", "NemesisTrackerEventFrame")
NT.timers = NT.timers or {}
NT.nextTimerId = NT.nextTimerId or 0

function NT:RegisterEvent(event)
    self.eventFrame:RegisterEvent(event)
end

function NT:UnregisterEvent(event)
    self.eventFrame:UnregisterEvent(event)
end


function NT:ScheduleRepeatingTimer(callback, interval)
    self.nextTimerId = self.nextTimerId + 1
    local id = self.nextTimerId
    local seconds = math.max(0.05, tonumber(interval) or 1)
    self.timers[id] = {
        callback = callback,
        remaining = seconds,
        interval = seconds,
    }
    return id
end


local function invokeTimerCallback(callback)
    if type(callback) == "function" then
        callback()
    elseif type(callback) == "string" and type(NT[callback]) == "function" then
        NT[callback](NT)
    end
end

function NT:OnUpdate(elapsed)
    for id, timer in pairs(self.timers) do
        timer.remaining = timer.remaining - elapsed
        if timer.remaining <= 0 then
            invokeTimerCallback(timer.callback)
            if self.timers[id] then
                if timer.interval then
                    timer.remaining = timer.interval
                else
                    self.timers[id] = nil
                end
            end
        end
    end
end

NT.eventFrame:SetScript("OnUpdate", function(_, elapsed)
    NT:OnUpdate(elapsed)
end)
