NemesisTracker = NemesisTracker or {}
local NT = NemesisTracker

local function splitPreserveEmpty(message, delimiter)
    local result = {}
    if message == nil then
        return result
    end

    local startIndex = 1
    while true do
        local delimiterIndex = string.find(message, delimiter, startIndex, true)
        if not delimiterIndex then
            table.insert(result, string.sub(message, startIndex))
            break
        end

        table.insert(result, string.sub(message, startIndex, delimiterIndex - 1))
        startIndex = delimiterIndex + string.len(delimiter)
        if startIndex > string.len(message) + 1 then
            table.insert(result, "")
            break
        end
    end

    return result
end

local function joinFields(fields, startIndex, delimiter)
    if not fields or not startIndex or startIndex > #fields then
        return ""
    end
    return table.concat(fields, delimiter or ":", startIndex)
end

function NT:ConfirmServerData()
    if self.data.serverDataConfirmed then return end
    self.data.serverDataConfirmed = true
    if self.UI then self.UI:RefreshAll() end
end

function NT:BeginBootstrap()
    self:ConfirmServerData()
    self.data.bootstrapActive = true
    wipe(self.data.nemeses)
    wipe(self.data.nemesesByUnitGuid)
    if self.UI then self.UI:RefreshAll() end
    if self.UnitUI then self.UnitUI:UpdateTargetPortrait() end
end

function NT:FinalizeBootstrap()
    self.data.bootstrapActive = false
    if self.UI then self.UI:RefreshAll() end
    if self.UnitUI then self.UnitUI:UpdateTargetPortrait() end
end

function NT:UpsertNemesisFromFields(fields, startIndex)
    -- V5 carries 17 fields after the opcode. Location is strictly MapID + X/Y.
    if not fields or not startIndex or #fields < (startIndex + 16) then return nil end

    local spawnId = tonumber(fields[startIndex])
    if not spawnId then return nil end

    local incoming = {
        spawnId = spawnId,
        creatureEntry = tonumber(fields[startIndex + 1]) or 0,
        unitGuid = fields[startIndex + 2] or "",
        name = fields[startIndex + 3] or "Unknown",
        mapId = tonumber(fields[startIndex + 4]) or 0,
        x = tonumber(fields[startIndex + 5]) or 0,
        y = tonumber(fields[startIndex + 6]) or 0,
        level = tonumber(fields[startIndex + 7]) or 0,
        rank = math.max(1, math.min(5, tonumber(fields[startIndex + 8]) or 1)),
        rankTier = fields[startIndex + 9] or "Marked",
        affixMask = tonumber(fields[startIndex + 10]) or 0,
        affixText = fields[startIndex + 11] or "None",
        targetGuid = tonumber(fields[startIndex + 12]) or 0,
        targetName = fields[startIndex + 13] or "",
        relation = fields[startIndex + 14] or "public",
        rewardClass = fields[startIndex + 15] or "none",
        threatClass = fields[startIndex + 16] or "low",
        serverName = fields[startIndex + 3] or "Unknown",
    }

    if not string.match(string.upper(incoming.unitGuid or ""), "^0X[%x]+$") then
        return nil
    end
    incoming.unitGuid = string.upper(incoming.unitGuid)

    local previous = self.data.nemeses[spawnId]
    if previous and previous.unitGuid then
        self.data.nemesesByUnitGuid[string.upper(previous.unitGuid)] = nil
    end
    self.data.nemeses[spawnId] = incoming
    self.data.nemesesByUnitGuid[incoming.unitGuid] = incoming
    if self.UI then self.UI:RefreshAll() end
    if self.UnitUI then self.UnitUI:UpdateTargetPortrait() end
    return incoming
end

function NT:RemoveNemesis(spawnId)
    if not spawnId then return end
    local previous = self.data.nemeses[spawnId]
    if previous and previous.unitGuid then
        self.data.nemesesByUnitGuid[string.upper(previous.unitGuid)] = nil
    end
    self.data.nemeses[spawnId] = nil
    if self.UI then self.UI:RefreshAll() end
    if self.UnitUI then self.UnitUI:UpdateTargetPortrait() end
end

function NT:ClearMapNemeses(mapId)
    mapId = tonumber(mapId)
    if not mapId then
        return
    end

    for spawnId, nemesis in pairs(self.data.nemeses) do
        if tonumber(nemesis.mapId) == mapId then
            if nemesis.unitGuid then
                self.data.nemesesByUnitGuid[string.upper(nemesis.unitGuid)] = nil
            end
            self.data.nemeses[spawnId] = nil
        end
    end

    if self.UI then self.UI:RefreshAll() end
    if self.UnitUI then self.UnitUI:UpdateTargetPortrait() end
end

function NT:HandleChunk(message)
    local fields = splitPreserveEmpty(message, ":")
    if fields[1] ~= "V5" or fields[2] ~= "CHUNK" then
        return
    end

    local chunkId = fields[3]
    local part = tonumber(fields[4]) or 0
    local total = tonumber(fields[5]) or 0
    local payload = joinFields(fields, 6, ":")
    if not chunkId or chunkId == "" or part <= 0 or total <= 0 then
        return
    end

    if not self.data.chunks[chunkId] then
        self.data.chunks[chunkId] = { total = total, parts = {} }
    end
    local state = self.data.chunks[chunkId]
    if state.total ~= total then
        state.total = total
    end
    state.parts[part] = payload

    local count = 0
    for _ in pairs(state.parts) do
        count = count + 1
    end
    if count < state.total then
        return
    end

    local rebuilt = ""
    for index = 1, state.total do
        rebuilt = rebuilt .. (state.parts[index] or "")
    end
    self.data.chunks[chunkId] = nil
    self:ParseServerPayload(rebuilt)
end

function NT:ParseServerPayload(payload)
    if not payload or payload == "" then
        return
    end

    local fields = splitPreserveEmpty(payload, ":")
    if fields[1] ~= "V5" then
        return
    end

    local opcode = fields[2]
    if not opcode then
        return
    end

    if opcode == "CHUNK" then
        self:HandleChunk(payload)
    elseif opcode == "BOOTSTRAP_BEGIN" then
        self:BeginBootstrap()
    elseif opcode == "BOOTSTRAP_ENTRY" then
        self:ConfirmServerData()
        self:UpsertNemesisFromFields(fields, 3)
    elseif opcode == "BOOTSTRAP_END" then
        self:ConfirmServerData()
        self:FinalizeBootstrap()
    elseif opcode == "UPSERT_VALIDATED" then
        self:ConfirmServerData()
        self:UpsertNemesisFromFields(fields, 3)
    elseif opcode == "REMOVE" then
        self:ConfirmServerData()
        self:RemoveNemesis(tonumber(fields[3]))
    elseif opcode == "MAP_CLEAR" then
        self:ConfirmServerData()
        self:ClearMapNemeses(fields[3])
    end
end

function NT:HandleAddonMessage(prefix, message, channel, sender)
    if prefix ~= self.prefix or channel ~= "WHISPER" or type(message) ~= "string" then return end

    if message == "V5:HELLO_ACK" then
        self.data.addonTransportVerified = true
        return
    end

    self:ParseServerPayload(message)
end

function NT:SendAddonHello()
    if type(SendAddonMessage) ~= "function" or type(UnitName) ~= "function" then return false end
    local playerName = UnitName("player")
    if not playerName or playerName == "" then return false end
    SendAddonMessage(self.prefix, "V5:HELLO", "WHISPER", playerName)
    return true
end
