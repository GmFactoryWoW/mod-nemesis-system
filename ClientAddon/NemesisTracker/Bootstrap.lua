-- Shared bootstrap for standard AddOns and FrameXML loading.
local addonName = ...

NemesisTracker_IS_FRAME_XML = addonName ~= "NemesisTracker"

if NemesisTracker_IS_FRAME_XML then
    NemesisTracker_ROOT = "Interface\\FrameXML\\NemesisTracker\\"

    -- FrameXML files are not associated with a TOC SavedVariables entry.
    -- 3.3.5a exposes RegisterForSave for persistent FrameXML globals.
    NemesisTrackerDB = NemesisTrackerDB or {}
    if type(RegisterForSave) == "function" then
        RegisterForSave("NemesisTrackerDB")
    end
else
    NemesisTracker_ROOT = "Interface\\AddOns\\NemesisTracker\\"
end

function NemesisTracker_Asset(relativePath)
    return NemesisTracker_ROOT .. relativePath
end
