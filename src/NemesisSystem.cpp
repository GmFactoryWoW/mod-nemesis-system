#include "AllCreatureScript.h"
#include "Chat.h"
#include "CharacterCache.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "Item.h"
#include "Mail.h"
#include "GuildMgr.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "UnitScript.h"
#include "WorldSession.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include "WorldPacket.h"


#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>


using namespace Acore::ChatCommands;

namespace
{
    enum NemesisAffix : uint32
    {
        NEMESIS_AFFIX_VAMPIRIC   = 1 << 0,
        NEMESIS_AFFIX_SWIFT      = 1 << 1,
        NEMESIS_AFFIX_JUGGERNAUT = 1 << 2,
        NEMESIS_AFFIX_SAVAGE     = 1 << 3,
        NEMESIS_AFFIX_SPELLWARD  = 1 << 4,
        NEMESIS_AFFIX_ENRAGED    = 1 << 5,
        NEMESIS_AFFIX_REGEN      = 1 << 6,
    };

    struct NemesisState
    {
        uint32 creatureEntry = 0;
        uint32 mapId = 0;
        float homeX = 0.0f;
        float homeY = 0.0f;
        uint8 rank = 1;
        uint32 affixMask = 0;
        uint32 baseHealth = 1;
        float baseScale = 1.0f;
        float baseMeleeMinDamage = BASE_MINDAMAGE;
        float baseMeleeMaxDamage = BASE_MAXDAMAGE;
        float baseRangedMinDamage = 0.0f;
        float baseRangedMaxDamage = 0.0f;
        uint32 baseAttackTime = BASE_ATTACK_TIME;
        uint32 baseRangeAttackTime = BASE_ATTACK_TIME;
        float baseRunSpeedRate = 1.0f;
        uint32 targetGuid = 0;
        uint32 lastPromotionAt = 0;
        uint32 lastVictimGuid = 0;
        uint32 createdAt = 0;
    };

    struct NemesisAddonView
    {
        ObjectGuid::LowType spawnId = 0;
        uint32 creatureEntry = 0;
        std::string unitGuid;
        std::string name;
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        uint8 level = 0;
        uint8 rank = 1;
        std::string rankTier;
        uint32 affixMask = 0;
        std::string affixText;
        uint32 targetGuid = 0;
        std::string targetName;
        std::string relation;
        std::string rewardClass;
        std::string threatClass;
    };

    using NemesisStore = std::unordered_map<ObjectGuid::LowType, NemesisState>;
    using NemesisTickStore = std::unordered_map<ObjectGuid::LowType, uint32>;

    NemesisStore ActiveNemeses;
    NemesisTickStore RegenTickAccumulators;
    NemesisTickStore VisualAuraTickAccumulators;
    NemesisTickStore DeadCleanupTickAccumulators;
    std::unordered_set<ObjectGuid::LowType> RewardedNemesisKills;
    std::recursive_mutex NemesisStoreMutex;
    bool CacheLoaded = false;

    std::string constexpr NEMESIS_ADDON_PREFIX = "Nemesis";
    size_t constexpr NEMESIS_ADDON_CHUNK_SIZE = 180;
    std::array<uint32, 5> constexpr NEMESIS_DEFAULT_VISUAL_AURA_SPELLS =
    {
        63130, // Trial of the Champion shield visual level 1
        63131, // Trial of the Champion shield visual level 2
        63132, // Trial of the Champion shield visual level 3
        61023, // Rank 4 visual
        41079  // Rank 5 visual
    };

    Creature* FindLoadedCreatureBySpawnId(Map* map, ObjectGuid::LowType spawnId);
    std::string GetServerLocalizedCreatureName(uint32 creatureEntry);
    std::string GetPlayerLocalizedCreatureName(Player const* player, uint32 creatureEntry);
    std::string GetNemesisDisplayName(Map* map, ObjectGuid::LowType spawnId, NemesisState const& state);
    void EnsureCacheLoaded();
    bool IsExpired(NemesisState const& state);

    bool IsEnabled()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.Enable", true);
    }


    uint8 GetMaxRank()
    {
        return std::max<uint8>(1, sConfigMgr->GetOption<uint8>("NemesisSystem.MaxRank", 5));
    }

    uint8 GetMinCreatureLevel()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.MinCreatureLevel", 1);
    }

    uint8 GetMaxCreatureLevel()
    {
        return std::max(GetMinCreatureLevel(), sConfigMgr->GetOption<uint8>("NemesisSystem.MaxCreatureLevel", 255));
    }

    uint8 GetPromotionLevelDiffMax()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.PromotionLevelDiffMax", 5);
    }

    uint8 GetTrivialKillLevelDelta()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.TrivialKillLevelDelta", 5);
    }

    uint8 GetRewardOverlevelDiffMax()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.RewardOverlevelDiffMax", 10);
    }

    uint8 GetRewardUnderlevelDiffMax()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.RewardUnderlevelDiffMax", 10);
    }

    float GetRewardUnderdogMaxMultiplier()
    {
        return std::max(1.0f, sConfigMgr->GetOption<float>("NemesisSystem.RewardUnderdogMaxMultiplier", 2.0f));
    }

    uint32 GetDecayHours()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.DecayHours", 48);
    }

    uint32 GetRankUpCooldownSeconds()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.RankUpCooldownSeconds", 300);
    }

    uint32 GetSameVictimCooldownSeconds()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.SameVictimCooldownSeconds", 900);
    }

    uint32 GetRewardItem(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardItem" : "NemesisSystem.BountyRewardItem", 0);
    }

    uint32 GetRewardItemCountMin()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.RewardItemCountMin", 1);
    }

    uint32 GetRewardItemCountMax()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.RewardItemCountMax", 1);
    }

    bool ShouldApplyRewardMultiplierToItems()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.RewardApplyMultiplierToItemCount", true);
    }

    bool ShouldApplyRankMultiplierToItems()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.RewardApplyRankMultiplierToItemCount", true);
    }

    uint32 GetRewardGold(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardGold" : "NemesisSystem.BountyRewardGold", revenge ? 10000 : 2500);
    }

    uint32 GetRewardItemPerRankBonus(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardItemPerRankBonus" : "NemesisSystem.BountyRewardItemPerRankBonus", 0);
    }

    uint32 GetRewardGoldPerRankBonus(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardGoldPerRankBonus" : "NemesisSystem.BountyRewardGoldPerRankBonus", revenge ? 2500 : 500);
    }

    uint8 GetVisualAuraTier(uint8 rank)
    {
        if (rank <= 1)
            return 1;

        return std::min<uint8>(rank, uint8(NEMESIS_DEFAULT_VISUAL_AURA_SPELLS.size()));
    }

    uint32 GetVisualAuraSpell(uint8 rank)
    {
        if (uint32 overrideSpell = sConfigMgr->GetOption<uint32>("NemesisSystem.VisualAuraSpell", 0))
            return overrideSpell;

        uint8 const tier = GetVisualAuraTier(rank);
        std::string const configKey = Acore::StringFormat("NemesisSystem.VisualAuraSpellRank{}", uint32(tier));
        return sConfigMgr->GetOption<uint32>(configKey, NEMESIS_DEFAULT_VISUAL_AURA_SPELLS[tier - 1]);
    }

    void AppendUniqueAuraSpell(std::vector<uint32>& auraSpells, uint32 auraSpell)
    {
        if (!auraSpell)
            return;

        if (std::find(auraSpells.begin(), auraSpells.end(), auraSpell) == auraSpells.end())
            auraSpells.push_back(auraSpell);
    }

    std::vector<uint32> GetVisualAuraSpellsForRank(uint8 rank)
    {
        std::vector<uint32> auraSpells;

        if (uint32 overrideSpell = sConfigMgr->GetOption<uint32>("NemesisSystem.VisualAuraSpell", 0))
        {
            auraSpells.push_back(overrideSpell);
            return auraSpells;
        }

        uint32 const primaryAuraSpell = GetVisualAuraSpell(rank);

        if (rank >= 4)
            AppendUniqueAuraSpell(auraSpells, GetVisualAuraSpell(3));

        AppendUniqueAuraSpell(auraSpells, primaryAuraSpell);
        return auraSpells;
    }

    void RemoveNemesisVisualAuras(Creature* creature)
    {
        if (!creature)
            return;

        std::array<uint32, 6> auraSpells =
        {
            sConfigMgr->GetOption<uint32>("NemesisSystem.VisualAuraSpell", 0),
            GetVisualAuraSpell(1),
            GetVisualAuraSpell(2),
            GetVisualAuraSpell(3),
            GetVisualAuraSpell(4),
            GetVisualAuraSpell(5)
        };

        for (size_t i = 0; i < auraSpells.size(); ++i)
        {
            uint32 const auraSpell = auraSpells[i];
            if (!auraSpell)
                continue;

            bool duplicate = false;
            for (size_t j = 0; j < i; ++j)
            {
                if (auraSpells[j] == auraSpell)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
                creature->RemoveAurasDueToSpell(auraSpell);
        }
    }

    void ApplyNemesisVisualAuras(Creature* creature, uint8 rank)
    {
        if (!creature)
            return;

        std::vector<uint32> const auraSpells = GetVisualAuraSpellsForRank(rank);
        for (uint32 const auraSpell : auraSpells)
        {
            if (creature->HasAura(auraSpell))
                continue;

            // Visual Nemesis markers are persistent auras, not gameplay casts.
            // AddAura applies the aura synchronously; no periodic refresh is required
            // for the initial promotion. AzerothCore handles the normal aura update
            // propagation to nearby clients.
            creature->AddAura(auraSpell, creature);
        }
    }

    uint32 GetAddonSnapshotIntervalSeconds()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.AddonSnapshotIntervalSeconds", 120);
    }



    bool HasAffix(NemesisState const& state, NemesisAffix affix)
    {
        return (state.affixMask & affix) != 0;
    }

    bool IsAllowedCreatureRank(uint32 rank)
    {
        switch (rank)
        {
            case CREATURE_ELITE_NORMAL:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowNormal", true);
            case CREATURE_ELITE_ELITE:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowElite", true);
            case CREATURE_ELITE_RARE:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowRare", true);
            case CREATURE_ELITE_RAREELITE:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowRareElite", true);
            case CREATURE_ELITE_WORLDBOSS:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowWorldBoss", false);
            default:
                return false;
        }
    }

    float GetScaleMultiplier(uint8 rank)
    {
        switch (rank)
        {
            case 1: return 1.20f;
            case 2: return 1.30f;
            case 3: return 1.40f;
            case 4: return 1.50f;
            default: return 1.60f;
        }
    }

    float GetHealthMultiplier(uint8 rank)
    {
        switch (rank)
        {
            case 1: return 1.50f;
            case 2: return 2.00f;
            case 3: return 2.50f;
            case 4: return 3.00f;
            default: return 3.50f;
        }
    }

    float GetDamageMultiplier(uint8 rank)
    {
        return GetHealthMultiplier(rank);
    }

    float GetVampiricHealPct()
    {
        return 0.50f;
    }

    float GetSwiftSpeedMultiplier()
    {
        return 1.50f;
    }

    float GetSwiftAttackTimeMultiplier()
    {
        return 0.70f;
    }

    float GetSavageDamageMultiplier()
    {
        return 1.25f;
    }

    float GetSpellwardDamageMultiplier()
    {
        return 0.70f;
    }

    float GetEnragedHealthPctThreshold()
    {
        return std::clamp(sConfigMgr->GetOption<float>("NemesisSystem.EnragedHealthPctThreshold", 30.0f), 1.0f, 99.0f);
    }

    float GetEnragedDamageMultiplier()
    {
        return std::max(1.0f, sConfigMgr->GetOption<float>("NemesisSystem.EnragedDamageMultiplier", 1.50f));
    }

    uint32 GetRegenerationIntervalMs()
    {
        return std::max<uint32>(1000, sConfigMgr->GetOption<uint32>("NemesisSystem.RegenerationIntervalMs", 5000));
    }

    float GetRegenerationHealthPct()
    {
        return std::clamp(sConfigMgr->GetOption<float>("NemesisSystem.RegenerationHealthPct", 3.0f), 0.1f, 100.0f);
    }

    std::string GetAffixList(uint32 affixMask)
    {
        std::ostringstream stream;
        bool first = true;

        auto append = [&](char const* name)
        {
            if (!first)
                stream << ", ";

            stream << name;
            first = false;
        };

        if (affixMask & NEMESIS_AFFIX_VAMPIRIC)
            append("Vampiric");

        if (affixMask & NEMESIS_AFFIX_SWIFT)
            append("Swift");

        if (affixMask & NEMESIS_AFFIX_JUGGERNAUT)
            append("Juggernaut");

        if (affixMask & NEMESIS_AFFIX_SAVAGE)
            append("Savage");

        if (affixMask & NEMESIS_AFFIX_SPELLWARD)
            append("Spellward");

        if (affixMask & NEMESIS_AFFIX_ENRAGED)
            append("Enraged");

        if (affixMask & NEMESIS_AFFIX_REGEN)
            append("Regenerating");

        if (first)
            return "None";

        return stream.str();
    }

    std::string SanitizeAddonField(std::string value)
    {
        std::replace(value.begin(), value.end(), ':', ';');
        std::replace(value.begin(), value.end(), '|', '/');
        std::replace(value.begin(), value.end(), '\t', ' ');
        std::replace(value.begin(), value.end(), '\r', ' ');
        std::replace(value.begin(), value.end(), '\n', ' ');
        return value;
    }

    float RoundToNearest(float value, float nearest)
    {
        if (nearest <= 0.0f)
            return value;

        return std::round(value / nearest) * nearest;
    }

    std::string GetRankTierLabel(uint8 rank)
    {
        switch (rank)
        {
            case 1: return "Marked";
            case 2: return "Hated";
            case 3: return "Relentless";
            case 4: return "Legendary";
            default: return "Mythic";
        }
    }

    std::string GetPlayerNameByGuidLow(uint32 guidLow)
    {
        if (!guidLow)
            return "";

        if (Player* target = HashMapHolder<Player>::Find(ObjectGuid::Create<HighGuid::Player>(guidLow)))
            return target->GetName();

        if (CharacterCacheEntry const* characterInfo = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guidLow)))
            return characterInfo->Name;

        return Acore::StringFormat("Player{}", guidLow);
    }

    void SendAddonPayload(Player* player, std::string const& payload)
    {
        if (!player || !player->GetSession())
            return;

        // Native WoW addon transport only. The client receives this as
        // CHAT_MSG_ADDON(prefix, payload, "WHISPER", sender).
        std::string const fullMessage = std::string(NEMESIS_ADDON_PREFIX) + "\t" + payload;
        size_t const len = fullMessage.length();

        WorldPacket data(SMSG_MESSAGECHAT, 1 + 4 + 8 + 4 + 8 + 4 + 1 + len + 1);
        data << uint8(CHAT_MSG_WHISPER);
        data << uint32(LANG_ADDON);
        data << uint64(player->GetGUID().GetRawValue());
        data << uint32(0);
        data << uint64(player->GetGUID().GetRawValue());
        data << uint32(len + 1);
        data << fullMessage;
        data << uint8(0);
        player->SendDirectMessage(&data);
    }

    void SendChunkedAddonPayload(Player* player, std::string const& payload)
    {
        if (payload.length() <= NEMESIS_ADDON_CHUNK_SIZE)
        {
            SendAddonPayload(player, payload);
            return;
        }

        std::string id = std::to_string(uint32(GameTime::GetGameTime().count())) + "_" + std::to_string(player->GetGUID().GetCounter());
        std::vector<std::string> chunks;
        size_t offset = 0;

        while (offset < payload.size())
        {
            size_t length = std::min(NEMESIS_ADDON_CHUNK_SIZE, payload.size() - offset);
            chunks.push_back(payload.substr(offset, length));
            offset += length;
        }

        for (size_t index = 0; index < chunks.size(); ++index)
            SendAddonPayload(player, Acore::StringFormat("V5:CHUNK:{}:{}:{}:{}", id, index + 1, chunks.size(), chunks[index]));
    }

    std::string GetRelationForPlayer(Player* player, NemesisState const& state)
    {
        if (!player)
            return "public";

        uint32 const playerGuid = player->GetGUID().GetCounter();
        if (state.targetGuid == playerGuid)
            return "own";

        if (Group* group = player->GetGroup())
            if (group->IsMember(ObjectGuid::Create<HighGuid::Player>(state.targetGuid)))
                return "party";

        if (player->GetGuildId())
            if (Player* target = HashMapHolder<Player>::Find(ObjectGuid::Create<HighGuid::Player>(state.targetGuid)))
                if (target->GetGuildId() == player->GetGuildId())
                    return "guild";

        return "public";
    }

    std::string GetRewardClassForPlayer(Player* player, NemesisState const& state)
    {
        if (!player)
            return "none";

        if (player->GetGUID().GetCounter() == state.targetGuid)
            return "revenge";

        if (Group* group = player->GetGroup())
            if (group->IsMember(ObjectGuid::Create<HighGuid::Player>(state.targetGuid)))
                return "shared";

        return "bounty";
    }

    std::string GetThreatClassForPlayer(Player* player, NemesisState const& state, uint8 creatureLevel)
    {
        uint32 score = state.rank;
        if (std::popcount(state.affixMask) >= 2)
            ++score;

        if (player)
        {
            int32 const levelDiff = int32(creatureLevel) - int32(player->GetLevel());
            if (levelDiff >= 5)
                score += 2;
            else if (levelDiff >= 2)
                ++score;
        }

        if (score <= 2)
            return "low";
        if (score <= 4)
            return "medium";
        if (score <= 6)
            return "high";

        return "extreme";
    }

    std::string FormatClientUnitGuid(ObjectGuid guid)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << guid.GetRawValue();
        return stream.str();
    }

    NemesisAddonView BuildAddonView(Player* player, ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        NemesisAddonView view;
        view.spawnId = spawnId;
        view.creatureEntry = state.creatureEntry;
        view.unitGuid = FormatClientUnitGuid(ObjectGuid::Create<HighGuid::Unit>(state.creatureEntry, spawnId));
        view.mapId = state.mapId;
        view.x = state.homeX;
        view.y = state.homeY;
        view.rank = state.rank;
        view.rankTier = GetRankTierLabel(state.rank);
        view.affixMask = state.affixMask;
        view.affixText = SanitizeAddonField(GetAffixList(state.affixMask));
        view.targetGuid = state.targetGuid;
        view.targetName = SanitizeAddonField(GetPlayerNameByGuidLow(state.targetGuid));
        view.relation = GetRelationForPlayer(player, state);
        view.rewardClass = GetRewardClassForPlayer(player, state);


        Map* playerMap = player ? player->GetMap() : nullptr;
        if (playerMap && playerMap->GetId() != state.mapId)
            playerMap = nullptr;

        if (Creature* liveCreature = FindLoadedCreatureBySpawnId(playerMap, spawnId))
        {
            view.unitGuid = FormatClientUnitGuid(liveCreature->GetGUID());
            view.name = SanitizeAddonField(GetPlayerLocalizedCreatureName(player, state.creatureEntry));
            view.x = liveCreature->GetPositionX();
            view.y = liveCreature->GetPositionY();
            view.level = liveCreature->GetLevel();
            view.threatClass = GetThreatClassForPlayer(player, state, view.level);
        }
        else
        {
            view.name = SanitizeAddonField(GetPlayerLocalizedCreatureName(player, state.creatureEntry));
            if (CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(state.creatureEntry))
                view.level = creatureTemplate->maxlevel;
            view.threatClass = GetThreatClassForPlayer(player, state, view.level);
        }

        return view;
    }

    std::string BuildAddonEntryPayload(char const* opcode, NemesisAddonView const& view)
    {
        // V5 location contract: MapID + world X/Y only. No zone/area IDs and
        // no server-derived map coordinates are transported.
        return Acore::StringFormat(
            "V5:{}:{}:{}:{}:{}:{}:{:.1f}:{:.1f}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
            opcode,
            uint64(view.spawnId),
            view.creatureEntry,
            view.unitGuid,
            view.name,
            view.mapId,
            RoundToNearest(view.x, 5.0f),
            RoundToNearest(view.y, 5.0f),
            uint32(view.level),
            uint32(view.rank),
            view.rankTier,
            view.affixMask,
            view.affixText,
            view.targetGuid,
            view.targetName,
            view.relation,
            view.rewardClass,
            view.threatClass);
    }

    std::string BuildRemovePayload(ObjectGuid::LowType spawnId, char const* reason)
    {
        return Acore::StringFormat("V5:REMOVE:{}:{}", uint64(spawnId), reason);
    }

    std::string BuildMapClearPayload(uint32 mapId)
    {
        return Acore::StringFormat("V5:MAP_CLEAR:{}", mapId);
    }


    std::vector<ObjectGuid::LowType> CollectBootstrapSpawnIds()
    {
        EnsureCacheLoaded();

        struct BootstrapEntry
        {
            ObjectGuid::LowType spawnId = 0;
            uint8 rank = 1;
            uint32 createdAt = 0;
        };

        std::vector<BootstrapEntry> matches;
        {
            std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
            matches.reserve(ActiveNemeses.size());
            for (NemesisStore::iterator itr = ActiveNemeses.begin(); itr != ActiveNemeses.end();)
            {
                if (IsExpired(itr->second))
                {
                    CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(itr->first));
                    itr = ActiveNemeses.erase(itr);
                    continue;
                }

                matches.push_back({ itr->first, itr->second.rank, itr->second.createdAt });
                ++itr;
            }
        }

        std::sort(matches.begin(), matches.end(), [](BootstrapEntry const& left, BootstrapEntry const& right)
        {
            if (left.rank != right.rank)
                return left.rank > right.rank;
            if (left.createdAt != right.createdAt)
                return left.createdAt < right.createdAt;
            return left.spawnId < right.spawnId;
        });

        std::vector<ObjectGuid::LowType> spawnIds;
        spawnIds.reserve(matches.size());
        for (BootstrapEntry const& entry : matches)
            spawnIds.push_back(entry.spawnId);
        return spawnIds;
    }

    void ForEachOnlinePlayer(std::function<void(Player*)> const& callback)
    {
        WorldSessionMgr::SessionMap const& sessionMap = sWorldSessionMgr->GetAllSessions();
        for (WorldSessionMgr::SessionMap::const_iterator itr = sessionMap.begin(); itr != sessionMap.end(); ++itr)
            if (Player* player = itr->second->GetPlayer())
                callback(player);
    }

    void SendNemesisBootstrap(Player* player)
    {
        if (!player)
            return;

        std::vector<ObjectGuid::LowType> const spawnIds = CollectBootstrapSpawnIds();

        SendAddonPayload(player, Acore::StringFormat("V5:BOOTSTRAP_BEGIN:{}:{}", spawnIds.size(), uint32(GameTime::GetGameTime().count())));

        for (ObjectGuid::LowType spawnId : spawnIds)
        {
            NemesisState state;
            {
                std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);

                NemesisStore::const_iterator itr = ActiveNemeses.find(spawnId);
                if (itr == ActiveNemeses.end())
                    continue;

                state = itr->second;
            }

            NemesisAddonView const view = BuildAddonView(player, spawnId, state);
            SendChunkedAddonPayload(player, BuildAddonEntryPayload("BOOTSTRAP_ENTRY", view));
        }

        SendAddonPayload(player, "V5:BOOTSTRAP_END");
    }

    void SendValidatedNemesisUpsert(Player* player, ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        if (!player)
            return;

        NemesisAddonView const view = BuildAddonView(player, spawnId, state);
        SendChunkedAddonPayload(player, BuildAddonEntryPayload("UPSERT_VALIDATED", view));
    }

    void BroadcastNemesisUpsert(ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        if (!spawnId)
            return;

        ForEachOnlinePlayer([&](Player* player)
        {
            SendValidatedNemesisUpsert(player, spawnId, state);
        });
    }

    void BroadcastNemesisRemove(ObjectGuid::LowType spawnId, char const* reason)
    {
        ForEachOnlinePlayer([&](Player* player)
        {
            SendAddonPayload(player, BuildRemovePayload(spawnId, reason));
        });
    }

    void BroadcastNemesisMapClear(uint32 mapId)
    {
        ForEachOnlinePlayer([&](Player* player)
        {
            SendAddonPayload(player, BuildMapClearPayload(mapId));
        });
    }

    Creature* FindLoadedCreatureBySpawnId(Map* map, ObjectGuid::LowType spawnId)
    {
        if (!map || !spawnId)
            return nullptr;

        auto bounds = map->GetCreatureBySpawnIdStore().equal_range(spawnId);
        if (bounds.first == bounds.second)
            return nullptr;

        return bounds.first->second;
    }

    std::string GetLocalizedCreatureName(uint32 creatureEntry, LocaleConstant locale)
    {
        CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(creatureEntry);
        if (!creatureTemplate)
            return Acore::StringFormat("entry {}", creatureEntry);

        std::string name = creatureTemplate->Name;
        if (CreatureLocale const* creatureLocale = sObjectMgr->GetCreatureLocale(creatureEntry))
            ObjectMgr::GetLocaleString(creatureLocale->Name, locale, name);

        return name;
    }

    std::string GetServerLocalizedCreatureName(uint32 creatureEntry)
    {
        return GetLocalizedCreatureName(creatureEntry, sWorld->GetDefaultDbcLocale());
    }

    std::string GetPlayerLocalizedCreatureName(Player const* player, uint32 creatureEntry)
    {
        LocaleConstant locale = sWorld->GetDefaultDbcLocale();
        if (player && player->GetSession())
            locale = player->GetSession()->GetSessionDbLocaleIndex();

        return GetLocalizedCreatureName(creatureEntry, locale);
    }

    std::string GetNemesisRankRpLabel(uint8 rank)
    {
        switch (rank)
        {
            case 1: return "Rang I - Traqué";
            case 2: return "Rang II - Dangereux";
            case 3: return "Rang III - Redoutable";
            case 4: return "Rang IV - Fléau";
            default: return "Rang V - Légendaire";
        }
    }

    void SendNemesisPromotionRpMessage(Player* victim, Creature* killer, uint8 rank, bool newlyCreated)
    {
        if (!victim || !victim->GetSession() || !killer)
            return;

        std::string const name = GetPlayerLocalizedCreatureName(victim, killer->GetEntry());
        std::string const rankLabel = GetNemesisRankRpLabel(rank);
        ChatHandler handler(victim->GetSession());

        if (newlyCreated)
            handler.PSendSysMessage("Votre défaite a marqué {}. Le PNJ qui vous a terrassé devient votre Némésis : {}.", name, rankLabel);
        else
            handler.PSendSysMessage("{} se nourrit de votre défaite et renforce sa puissance de Némésis : {}.", name, rankLabel);
    }

    std::string GetNemesisDisplayName(Map* /*map*/, ObjectGuid::LowType /*spawnId*/, NemesisState const& state)
    {
        return GetServerLocalizedCreatureName(state.creatureEntry);
    }

    bool IsExpired(NemesisState const& state)
    {
        uint32 const decayHours = GetDecayHours();
        if (!decayHours || !state.createdAt)
            return false;

        return state.createdAt + (decayHours * 60u * 60u) < uint32(GameTime::GetGameTime().count());
    }

    uint32 GetRankUpCooldownRemaining(NemesisState const& state)
    {
        uint32 const cooldown = GetRankUpCooldownSeconds();
        if (!cooldown)
            return 0;

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const expiresAt = state.lastPromotionAt + cooldown;

        if (!state.lastPromotionAt || expiresAt <= now)
            return 0;

        return expiresAt - now;
    }

    uint32 GetSameVictimCooldownRemaining(NemesisState const& state, uint32 victimGuid)
    {
        uint32 const cooldown = GetSameVictimCooldownSeconds();
        if (!cooldown)
            return 0;

        if (state.lastVictimGuid != victimGuid)
            return 0;

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const expiresAt = state.lastPromotionAt + cooldown;

        if (!state.lastPromotionAt || expiresAt <= now)
            return 0;

        return expiresAt - now;
    }

    void EnsureCacheLoaded()
    {
        if (CacheLoaded)
            return;

        CacheLoaded = true;

        QueryResult result = CharacterDatabase.Query(
            "SELECT `guid`, `creature_entry`, `map_id`, `pos_x`, `pos_y`, `rank`, `affix_mask`, `base_health`, `base_scale`, `base_melee_min_damage`, "
            "`base_melee_max_damage`, `base_ranged_min_damage`, `base_ranged_max_damage`, `base_attack_time`, `base_range_attack_time`, `base_run_speed_rate`, `nemesis_target_guid`, `last_promotion_at`, `last_victim_guid`, "
            "UNIX_TIMESTAMP(`creation_date`) FROM `character_nemesis`");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();

            ObjectGuid::LowType const spawnId = fields[0].Get<uint64>();

            NemesisState state;
            state.creatureEntry = fields[1].Get<uint32>();
            state.mapId = fields[2].Get<uint32>();
            state.homeX = fields[3].Get<float>();
            state.homeY = fields[4].Get<float>();
            state.rank = std::clamp<uint8>(fields[5].Get<uint8>(), 1, GetMaxRank());
            state.affixMask = fields[6].Get<uint32>();
            state.baseHealth = fields[7].Get<uint32>();
            state.baseScale = fields[8].Get<float>();
            state.baseMeleeMinDamage = fields[9].Get<float>();
            state.baseMeleeMaxDamage = fields[10].Get<float>();
            state.baseRangedMinDamage = fields[11].Get<float>();
            state.baseRangedMaxDamage = fields[12].Get<float>();
            state.baseAttackTime = fields[13].Get<uint32>();
            state.baseRangeAttackTime = fields[14].Get<uint32>();
            state.baseRunSpeedRate = fields[15].Get<float>();
            state.targetGuid = fields[16].Get<uint32>();
            state.lastPromotionAt = fields[17].Get<uint32>();
            state.lastVictimGuid = fields[18].Get<uint32>();
            state.createdAt = fields[19].Get<uint32>();

            if (IsExpired(state))
            {
                CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
                continue;
            }

            ActiveNemeses[spawnId] = state;
        }
        while (result->NextRow());
    }

    bool TryGetNemesisState(ObjectGuid::LowType spawnId, NemesisState& state)
    {
        if (!spawnId)
            return false;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        EnsureCacheLoaded();

        NemesisStore::iterator itr = ActiveNemeses.find(spawnId);
        if (itr == ActiveNemeses.end())
            return false;

        if (IsExpired(itr->second))
        {
            CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
            ActiveNemeses.erase(itr);
            return false;
        }

        state = itr->second;
        return true;
    }

    bool TryGetNemesisState(Creature* creature, NemesisState& state)
    {
        if (!creature || !creature->GetSpawnId())
            return false;

        return TryGetNemesisState(creature->GetSpawnId(), state);
    }

    void SaveNemesisState(Creature* creature, NemesisState const& state)
    {
        if (!creature)
            return;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);

        if (!creature->GetSpawnId())
            return;

        EnsureCacheLoaded();

        NemesisState storedState = state;
        float const homeX = creature->GetPositionX();
        float const homeY = creature->GetPositionY();
        storedState.homeX = homeX;
        storedState.homeY = homeY;

        CharacterDatabase.Execute(
            "REPLACE INTO `character_nemesis` "
            "(`guid`, `creature_entry`, `map_id`, `pos_x`, `pos_y`, `rank`, `affix_mask`, `base_health`, `base_scale`, "
            "`base_melee_min_damage`, `base_melee_max_damage`, `base_ranged_min_damage`, `base_ranged_max_damage`, `base_attack_time`, `base_range_attack_time`, `base_run_speed_rate`, `nemesis_target_guid`, `last_promotion_at`, `last_victim_guid`, `creation_date`) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, FROM_UNIXTIME({}))",
            uint64(creature->GetSpawnId()),
            creature->GetEntry(),
            creature->GetMapId(),
            homeX,
            homeY,
            storedState.rank,
            storedState.affixMask,
            storedState.baseHealth,
            storedState.baseScale,
            storedState.baseMeleeMinDamage,
            storedState.baseMeleeMaxDamage,
            storedState.baseRangedMinDamage,
            storedState.baseRangedMaxDamage,
            storedState.baseAttackTime,
            storedState.baseRangeAttackTime,
            storedState.baseRunSpeedRate,
            storedState.targetGuid,
            storedState.lastPromotionAt,
            storedState.lastVictimGuid,
            storedState.createdAt ? storedState.createdAt : uint32(GameTime::GetGameTime().count()));

        ActiveNemeses[creature->GetSpawnId()] = storedState;
    }

    void DeleteNemesisState(ObjectGuid::LowType spawnId, char const* reason = "cleared")
    {
        if (!spawnId)
            return;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        EnsureCacheLoaded();

        ActiveNemeses.erase(spawnId);
        RegenTickAccumulators.erase(spawnId);
        VisualAuraTickAccumulators.erase(spawnId);
        DeadCleanupTickAccumulators.erase(spawnId);
        CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
        BroadcastNemesisRemove(spawnId, reason);
    }

    void DeleteNemesisState(Creature* creature, char const* reason = "cleared")
    {
        if (!creature || !creature->GetSpawnId())
            return;

        DeleteNemesisState(creature->GetSpawnId(), reason);
    }

    void EraseRegenAccumulator(Creature* creature)
    {
        if (!creature || !creature->GetSpawnId())
            return;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        RegenTickAccumulators.erase(creature->GetSpawnId());
    }

    bool UpdateRegenAccumulator(Creature* creature, uint32 diff, uint32 interval)
    {
        if (!creature || !creature->GetSpawnId())
            return false;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        uint32& accumulator = RegenTickAccumulators[creature->GetSpawnId()];
        accumulator += diff;
        if (accumulator < interval)
            return false;

        accumulator %= interval;
        return true;
    }

    void EraseVisualAuraAccumulator(Creature* creature)
    {
        if (!creature || !creature->GetSpawnId())
            return;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        VisualAuraTickAccumulators.erase(creature->GetSpawnId());
    }

    bool UpdateVisualAuraAccumulator(Creature* creature, uint32 diff, uint32 interval)
    {
        if (!creature || !creature->GetSpawnId())
            return false;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        uint32& accumulator = VisualAuraTickAccumulators[creature->GetSpawnId()];
        accumulator += diff;
        if (accumulator < interval)
            return false;

        accumulator %= interval;
        return true;
    }



    bool UpdateDeadCleanupAccumulator(Creature* creature, uint32 diff, uint32 delay)
    {
        if (!creature || !creature->GetSpawnId())
            return false;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        uint32& accumulator = DeadCleanupTickAccumulators[creature->GetSpawnId()];
        accumulator += diff;
        return accumulator >= delay;
    }

    void EraseDeadCleanupAccumulator(Creature* creature)
    {
        if (!creature || !creature->GetSpawnId())
            return;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        DeadCleanupTickAccumulators.erase(creature->GetSpawnId());
    }

    NemesisState BuildInitialNemesisState(Creature* killer, Player* killed)
    {
        NemesisState state;
        state.creatureEntry = killer->GetEntry();
        state.mapId = killer->GetMapId();
        state.rank = 1;
        state.affixMask = 0;
        state.homeX = killer->GetPositionX();
        state.homeY = killer->GetPositionY();
        state.baseHealth = std::max<uint32>(1, killer->GetCreateHealth());
        state.baseScale = killer->GetNativeObjectScale();
        state.baseMeleeMinDamage = std::max<float>(BASE_MINDAMAGE, killer->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE, 0));
        state.baseMeleeMaxDamage = std::max<float>(BASE_MAXDAMAGE, killer->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE, 0));
        state.baseRangedMinDamage = std::max<float>(0.0f, killer->GetWeaponDamageRange(RANGED_ATTACK, MINDAMAGE, 0));
        state.baseRangedMaxDamage = std::max<float>(0.0f, killer->GetWeaponDamageRange(RANGED_ATTACK, MAXDAMAGE, 0));
        state.baseAttackTime = killer->GetCreatureTemplate()->BaseAttackTime;
        state.baseRangeAttackTime = killer->GetCreatureTemplate()->RangeAttackTime;
        state.baseRunSpeedRate = killer->GetSpeedRate(MOVE_RUN);
        state.targetGuid = killed->GetGUID().GetCounter();
        state.createdAt = uint32(GameTime::GetGameTime().count());
        return state;
    }

    void RollAffixes(NemesisState& state)
    {
        std::array<uint32, 7> const affixes = { NEMESIS_AFFIX_VAMPIRIC, NEMESIS_AFFIX_SWIFT, NEMESIS_AFFIX_JUGGERNAUT, NEMESIS_AFFIX_SAVAGE, NEMESIS_AFFIX_SPELLWARD, NEMESIS_AFFIX_ENRAGED, NEMESIS_AFFIX_REGEN };
        uint32 affixMask = state.affixMask;
        uint32 const desiredAffixCount = state.rank >= 5 ? 3u : (state.rank >= 3 ? 2u : 1u);

        while (static_cast<uint32>(std::popcount(affixMask)) < desiredAffixCount)
            affixMask |= affixes[urand(0, affixes.size() - 1)];

        state.affixMask = affixMask;
    }

    void ApplyJuggernautImmunity(Creature* creature, bool apply)
    {
        uint32 const placeholderId = 0;

        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_SNARE, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_ROOT, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_FEAR, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_STUN, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_SLEEP, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_CHARM, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_SAPPED, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_POLYMORPH, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_DISORIENTED, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_FREEZE, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_HORROR, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_BANISH, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK_DEST, apply);
    }

    void ResetCreatureToBaseState(Creature* creature, NemesisState const& state)
    {
        if (!creature)
            return;

        creature->SetObjectScale(state.baseScale);
        creature->SetCreateHealth(state.baseHealth);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(state.baseHealth));
        creature->SetMaxHealth(state.baseHealth);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, state.baseMeleeMinDamage, 0);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, state.baseMeleeMaxDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, state.baseMeleeMinDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, state.baseMeleeMaxDamage, 0);
        creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, state.baseRangedMinDamage, 0);
        creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, state.baseRangedMaxDamage, 0);
        creature->SetAttackTime(BASE_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(OFF_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(RANGED_ATTACK, state.baseRangeAttackTime);
        creature->SetSpeedRate(MOVE_RUN, state.baseRunSpeedRate);
        creature->UpdateSpeed(MOVE_RUN, true);
        ApplyJuggernautImmunity(creature, false);

        RemoveNemesisVisualAuras(creature);

        creature->UpdateAllStats();

        if (creature->IsAlive())
            creature->SetFullHealth();
    }

    bool IsPlayerEligibleForRevenge(Player* player, NemesisState const& state)
    {
        if (!player)
            return false;

        if (player->GetGUID().GetCounter() == state.targetGuid)
            return true;

        Group* group = player->GetGroup();
        if (!group)
            return false;

        return group->IsMember(ObjectGuid::Create<HighGuid::Player>(state.targetGuid));
    }

    bool TryClaimNemesisKillReward(ObjectGuid::LowType spawnId)
    {
        if (!spawnId)
            return false;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        return RewardedNemesisKills.insert(spawnId).second;
    }

    void ResetNemesisKillRewardClaim(ObjectGuid::LowType spawnId)
    {
        if (!spawnId)
            return;

        std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
        RewardedNemesisKills.erase(spawnId);
    }

    struct RewardRecipients
    {
        std::vector<Player*> players;
        uint8 highestLevel = 0;
    };

    RewardRecipients CollectRewardRecipients(Player* killer, Creature* killed)
    {
        RewardRecipients recipients;
        if (!killer || !killed)
            return recipients;

        auto addRecipient = [&](Player* player)
        {
            if (!player)
                return;

            recipients.players.push_back(player);
            recipients.highestLevel = std::max<uint8>(recipients.highestLevel, player->GetLevel());
        };

        Group* group = killer->GetGroup();
        if (!group)
        {
            addRecipient(killer);
            return recipients;
        }

        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            // AzerothCore's group reward distance check accounts for both the
            // living player position and a nearby released corpse. The killer
            // is always eligible because they delivered the fatal blow.
            if (member != killer && !member->IsAtGroupRewardDistance(killed))
                continue;

            addRecipient(member);
        }

        if (recipients.players.empty())
            addRecipient(killer);

        return recipients;
    }

    float GetRewardMultiplier(uint8 creatureLevel, uint8 referenceLevel)
    {
        int32 const levelDiff = int32(referenceLevel) - int32(creatureLevel);
        if (levelDiff > 0)
        {
            uint8 const maxDiff = GetRewardOverlevelDiffMax();
            if (!maxDiff)
                return 0.0f;

            float const multiplier = 1.0f - (float(levelDiff) / float(maxDiff));
            return std::clamp(multiplier, 0.0f, 1.0f);
        }

        if (levelDiff < 0)
        {
            uint8 const maxDiff = GetRewardUnderlevelDiffMax();
            float const maxMultiplier = GetRewardUnderdogMaxMultiplier();
            if (!maxDiff || maxMultiplier <= 1.0f)
                return 1.0f;

            uint32 const underlevelDiff = uint32(-levelDiff);
            float const progress = float(std::min<uint32>(underlevelDiff, maxDiff)) / float(maxDiff);
            return 1.0f + ((maxMultiplier - 1.0f) * progress);
        }

        return 1.0f;
    }

    uint32 GetScaledItemCount(uint32 baseCount, float multiplier)
    {
        if (!baseCount || multiplier <= 0.0f)
            return 0;

        float const scaledCount = float(baseCount) * multiplier;
        uint32 scaledItems = uint32(scaledCount);
        float const fractional = scaledCount - float(scaledItems);

        if (fractional > 0.0f && frand(0.0f, 1.0f) < fractional)
            ++scaledItems;

        return scaledItems;
    }

    uint32 GetScaledGold(uint32 baseGold, float multiplier)
    {
        if (!baseGold || multiplier <= 0.0f)
            return 0;

        return uint32((float(baseGold) * multiplier) + 0.5f);
    }

    struct RewardGrantResult
    {
        uint32 itemId = 0;
        uint32 itemCount = 0;
        bool itemGranted = false;
        bool itemMailed = false;
    };

    bool SendNemesisRewardByMail(Player* player, uint32 itemId, uint32 itemCount, std::string const& nemesisName, std::string const& targetName)
    {
        if (!player || !itemId || !itemCount)
            return false;

        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        if (!itemTemplate)
            return false;

        MailDraft draft("Butin de Némésis", Acore::StringFormat(
            "La chute de {} a lavé l'affront fait à {}.$B$B"
            "Votre récompense n'a pu trouver place dans vos sacs. Elle vous est donc remise par courrier.",
            nemesisName, targetName));

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        uint32 remaining = itemCount;
        uint32 const maxStack = std::max<uint32>(1, itemTemplate->GetMaxStackSize());
        uint32 attachments = 0;

        while (remaining && attachments < MAX_MAIL_ITEMS)
        {
            uint32 const stackCount = std::min(remaining, maxStack);
            Item* item = Item::CreateItem(itemId, stackCount, nullptr);
            if (!item)
                return false;

            item->SaveToDB(trans);
            draft.AddItem(item);
            remaining -= stackCount;
            ++attachments;
        }

        // Reward quantities are normally small. Refuse an incomplete mail rather
        // than silently dropping items if a custom configuration exceeds the
        // attachment capacity of a single WoW mail.
        if (remaining)
            return false;

        draft.SendMailTo(trans, MailReceiver(player, player->GetGUID().GetCounter()), MailSender(MAIL_CREATURE, 34337));
        CharacterDatabase.CommitTransaction(trans);
        return true;
    }

    RewardGrantResult GrantReward(Player* player, bool revenge, uint8 rank, float rewardMultiplier, std::string const& nemesisName, std::string const& targetName)
    {
        RewardGrantResult result;
        if (!player)
            return result;

        uint32 const rankBonusSteps = rank > 0 ? uint32(rank - 1) : 0;

        // Min/Max define the random BASE item quantity. Rank bonus and the
        // optional level multiplier are applied afterwards.
        uint32 const minItems = GetRewardItemCountMin();
        uint32 configuredMaxItems = GetRewardItemCountMax();
        if (!configuredMaxItems)
            configuredMaxItems = minItems;
        uint32 const maxItems = std::max(minItems, configuredMaxItems);

        uint32 const randomBaseItemCount = maxItems > minItems ? urand(minItems, maxItems) : minItems;
        uint32 const baseItemCount = randomBaseItemCount + (GetRewardItemPerRankBonus(revenge) * rankBonusSteps);

        // Rank directly reinforces item rewards when enabled: N1 x1, N2 x2, ... N5 x5.
        uint32 const rankMultiplier = ShouldApplyRankMultiplierToItems() ? std::max<uint32>(1, uint32(rank)) : 1;
        uint32 const rankedItemCount = baseItemCount * rankMultiplier;

        uint32 const itemCount = ShouldApplyRewardMultiplierToItems()
            ? GetScaledItemCount(rankedItemCount, rewardMultiplier)
            : (rewardMultiplier > 0.0f ? rankedItemCount : 0);

        uint32 const baseGold = GetRewardGold(revenge) + (GetRewardGoldPerRankBonus(revenge) * rankBonusSteps);
        uint32 const gold = GetScaledGold(baseGold, rewardMultiplier);

        result.itemId = GetRewardItem(revenge);
        result.itemCount = itemCount;

        if (result.itemId && result.itemCount)
        {
            ItemPosCountVec dest;
            InventoryResult const storeResult = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, result.itemId, result.itemCount);
            if (storeResult == EQUIP_ERR_OK)
            {
                player->StoreNewItem(dest, result.itemId, true);
                result.itemGranted = true;
            }
            else if (SendNemesisRewardByMail(player, result.itemId, result.itemCount, nemesisName, targetName))
            {
                result.itemGranted = true;
                result.itemMailed = true;
            }
        }

        if (gold)
            player->ModifyMoney(int32(gold), true);

        return result;
    }

    void NotifyNemesisItemReward(Player* player, Player* killer, uint32 targetGuid, bool revenge, RewardGrantResult const& reward, std::string const& nemesisName, std::string const& targetName)
    {
        if (!player || !killer || !player->GetSession() || !reward.itemGranted || !reward.itemId || !reward.itemCount)
            return;

        std::string itemName = Acore::StringFormat("objet {}", reward.itemId);
        if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(reward.itemId))
            itemName = itemTemplate->Name1;

        uint32 const playerGuid = player->GetGUID().GetCounter();
        uint32 const killerGuid = killer->GetGUID().GetCounter();
        bool const killerAvengedSelf = killerGuid == targetGuid;
        bool const recipientIsTarget = playerGuid == targetGuid;
        bool const recipientIsKiller = playerGuid == killerGuid;
        std::string const killerName = killer->GetName();

        std::string message;

        // Message role is determined from the recipient first. In particular,
        // the original Nemesis target must never be told that they delivered
        // the killing blow when another group member actually killed it.
        if (recipientIsTarget)
        {
            if (killerAvengedSelf)
                message = Acore::StringFormat("Vous avez enfin terrassé {}, votre propre Némésis. L'affront est lavé dans le sang : {} x{} vous revient.", nemesisName, itemName, reward.itemCount);
            else
                message = Acore::StringFormat("Votre groupe a terrassé {} — votre propre Némésis. {} a porté le coup fatal et vos compagnons ont vengé votre défaite : {} x{} vous revient.", nemesisName, killerName, itemName, reward.itemCount);
        }
        else if (killerAvengedSelf)
        {
            message = Acore::StringFormat("{} a terrassé {}, sa propre Némésis. Vous avez pris part à cette vengeance et recevez {} x{}.", killerName, nemesisName, itemName, reward.itemCount);
        }
        else if (revenge)
        {
            if (recipientIsKiller)
                message = Acore::StringFormat("Vous avez terrassé {} et vengé votre compagnon {}. Pour cet acte, {} x{} vous revient.", nemesisName, targetName, itemName, reward.itemCount);
            else
                message = Acore::StringFormat("{} a terrassé {} et vengé votre compagnon {}. Vous avez combattu à leurs côtés et recevez {} x{}.", killerName, nemesisName, targetName, itemName, reward.itemCount);
        }
        else
        {
            if (recipientIsKiller)
                message = Acore::StringFormat("Vous avez abattu {}, la Némésis qui traquait {}. La prime est vôtre : {} x{}.", nemesisName, targetName, itemName, reward.itemCount);
            else
                message = Acore::StringFormat("{} a abattu {}, la Némésis qui traquait {}. Votre participation vous rapporte {} x{}.", killerName, nemesisName, targetName, itemName, reward.itemCount);
        }

        if (reward.itemMailed)
            message += " Vos sacs ne pouvant accueillir ce butin, la récompense vous a été envoyée par courrier.";

        ChatHandler(player->GetSession()).SendSysMessage(message);
    }

    void ProcessNemesisKillRewards(Player* killer, Creature* killed)
    {
        if (!killer || !killed)
            return;

        ObjectGuid::LowType const spawnId = killed->GetSpawnId();
        if (!spawnId)
            return;

        NemesisState state;
        if (!TryGetNemesisState(killed, state))
            return;

        if (!TryClaimNemesisKillReward(spawnId))
            return;

        RewardRecipients const recipients = CollectRewardRecipients(killer, killed);
        float const rewardMultiplier = GetRewardMultiplier(killed->GetLevel(), recipients.highestLevel);
        std::string const nemesisName = killed->GetName();
        std::string const targetName = GetPlayerNameByGuidLow(state.targetGuid);

        if (rewardMultiplier > 0.0f)
        {
            for (Player* recipient : recipients.players)
            {
                // A recipient may qualify both for a generic kill/bounty and
                // for revenge. Revenge always has priority and only one reward
                // can be granted to that recipient for this Nemesis death.
                bool const revenge = IsPlayerEligibleForRevenge(recipient, state);
                RewardGrantResult const reward = GrantReward(recipient, revenge, state.rank, rewardMultiplier, nemesisName, targetName);
                NotifyNemesisItemReward(recipient, killer, state.targetGuid, revenge, reward, nemesisName, targetName);
            }
        }

        // Keep the claim until this spawn is promoted again. This makes the
        // UnitScript and PlayerScript death paths idempotent.
        DeleteNemesisState(killed, "slain");
    }

    bool IsEligibleNemesisKill(Creature* killer, Player* killed)
    {
        if (!IsEnabled() || !killer || !killed)
            return false;

        if (!killer->IsInWorld() || !killer->GetSpawnId())
            return false;

        Map* map = killer->GetMap();
        if (!map || map->IsDungeon() || map->IsBattlegroundOrArena() || map->IsRaid())
            return false;

        if (killed->IsInSanctuary())
            return false;

        if (killer->IsPet() || killer->IsCritter())
            return false;

        if (killer->GetLevel() < GetMinCreatureLevel() || killer->GetLevel() > GetMaxCreatureLevel())
            return false;

        int32 levelDiff = int32(killer->GetLevel()) - int32(killed->GetLevel());
        if (levelDiff < 0)
            levelDiff = -levelDiff;

        if (levelDiff > GetPromotionLevelDiffMax())
            return false;

        if (!IsAllowedCreatureRank(killer->GetCreatureTemplate()->rank))
            return false;

        if (killer->IsDungeonBoss())
            return false;

        if (killer->isWorldBoss() && !sConfigMgr->GetOption<bool>("NemesisSystem.AllowWorldBoss", false))
            return false;

        if ((killer->GetLevel() + GetTrivialKillLevelDelta()) < killed->GetLevel())
            return false;

        NemesisState state;
        if (TryGetNemesisState(killer->GetSpawnId(), state))
        {
            if (state.rank >= GetMaxRank())
                return false;

            if (GetRankUpCooldownRemaining(state) > 0)
                return false;

            if (GetSameVictimCooldownRemaining(state, killed->GetGUID().GetCounter()) > 0)
                return false;
        }

        return true;
    }

    void ApplyNemesisState(Creature* creature, NemesisState const& state)
    {
        if (!creature)
            return;

        RemoveNemesisVisualAuras(creature);

        uint32 const scaledHealth = std::max<uint32>(1, uint32(float(state.baseHealth) * GetHealthMultiplier(state.rank)));
        float const meleeMinDamage = std::max<float>(BASE_MINDAMAGE, state.baseMeleeMinDamage * GetDamageMultiplier(state.rank));
        float const meleeMaxDamage = std::max<float>(BASE_MAXDAMAGE, state.baseMeleeMaxDamage * GetDamageMultiplier(state.rank));
        float const rangedMinDamage = std::max<float>(0.0f, state.baseRangedMinDamage * GetDamageMultiplier(state.rank));
        float const rangedMaxDamage = std::max<float>(0.0f, state.baseRangedMaxDamage * GetDamageMultiplier(state.rank));

        creature->SetObjectScale(state.baseScale * GetScaleMultiplier(state.rank));
        creature->SetCreateHealth(scaledHealth);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(scaledHealth));
        creature->SetMaxHealth(scaledHealth);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, meleeMinDamage, 0);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, meleeMaxDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, meleeMinDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, meleeMaxDamage, 0);
        creature->SetAttackTime(BASE_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(OFF_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(RANGED_ATTACK, state.baseRangeAttackTime);

        if (state.baseRangedMinDamage > 0.0f || state.baseRangedMaxDamage > 0.0f)
        {
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, rangedMinDamage, 0);
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, rangedMaxDamage, 0);
        }

        creature->SetSpeedRate(MOVE_RUN, state.baseRunSpeedRate);
        creature->UpdateSpeed(MOVE_RUN, true);

        if (HasAffix(state, NEMESIS_AFFIX_SWIFT))
        {
            creature->SetSpeedRate(MOVE_RUN, state.baseRunSpeedRate * GetSwiftSpeedMultiplier());
            creature->UpdateSpeed(MOVE_RUN, true);
            creature->SetAttackTime(BASE_ATTACK, uint32(float(state.baseAttackTime) * GetSwiftAttackTimeMultiplier()));
            creature->SetAttackTime(OFF_ATTACK, uint32(float(state.baseAttackTime) * GetSwiftAttackTimeMultiplier()));
            creature->SetAttackTime(RANGED_ATTACK, uint32(float(state.baseRangeAttackTime) * GetSwiftAttackTimeMultiplier()));
        }

        if (HasAffix(state, NEMESIS_AFFIX_JUGGERNAUT))
            ApplyJuggernautImmunity(creature, true);

        creature->UpdateAllStats();
        creature->SetMaxHealth(scaledHealth);

        if (creature->IsAlive())
            creature->SetHealth(scaledHealth);
        else
            creature->SetHealth(std::min<uint32>(creature->GetHealth(), scaledHealth));

        ApplyNemesisVisualAuras(creature, state.rank);
    }

    bool IsBelowEnrageThreshold(Creature* creature)
    {
        if (!creature || !creature->GetMaxHealth())
            return false;

        float const healthPct = (100.0f * float(creature->GetHealth())) / float(creature->GetMaxHealth());
        return healthPct <= GetEnragedHealthPctThreshold();
    }

    void PromoteNemesis(Creature* killer, Player* killed)
    {
        NemesisState state;
        bool const existed = TryGetNemesisState(killer, state);
        uint8 const previousRank = existed ? state.rank : 0;
        uint32 const now = uint32(GameTime::GetGameTime().count());

        // Max-rank Nemeses no longer progress from additional player kills.
        // Keep this guard here as a second line of defense in case this
        // function is called from another path in the future.
        if (existed && state.rank >= GetMaxRank())
            return;

        if (existed)
        {
            if (state.rank < GetMaxRank())
                ++state.rank;
        }
        else
            state = BuildInitialNemesisState(killer, killed);

        bool const rankChanged = !existed || state.rank > previousRank;

        state.creatureEntry = killer->GetEntry();
        state.mapId = killer->GetMapId();
        state.targetGuid = killed->GetGUID().GetCounter();
        state.lastPromotionAt = now;
        state.lastVictimGuid = killed->GetGUID().GetCounter();
        if (!state.createdAt)
            state.createdAt = now;
        RollAffixes(state);

        // This spawn starts a new Nemesis reward lifecycle.
        ResetNemesisKillRewardClaim(killer->GetSpawnId());
        SaveNemesisState(killer, state);
        ApplyNemesisState(killer, state);
        killer->SetFullHealth();

        // Promotion is the authoritative moment at which the Nemesis visual must
        // become visible. Re-assert the rank aura synchronously after every promotion
        // mutation; the periodic refresh remains repair-only.
        ApplyNemesisVisualAuras(killer, state.rank);

        if (ObjectGuid::LowType const spawnId = killer->GetSpawnId())
            BroadcastNemesisUpsert(spawnId, state);

        if (rankChanged)
            SendNemesisPromotionRpMessage(killed, killer, state.rank, !existed);
    }
}

class NemesisSystemPlayerScript : public PlayerScript
{
public:
    NemesisSystemPlayerScript() : PlayerScript("NemesisSystemPlayerScript", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE, PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET, PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE }) { }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        if (!player || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
            return;

        std::string const prefix = std::string(NEMESIS_ADDON_PREFIX) + "\t";
        if (msg.rfind(prefix, 0) != 0)
            return;

        std::string const payload = msg.substr(prefix.length());
        if (payload == "V5:HELLO")
        {
            // This ACK proves client -> core reception. It is sent back through
            // the same native addon channel and proves core -> client reception.
            SendAddonPayload(player, "V5:HELLO_ACK");
            SendNemesisBootstrap(player);
        }
    }

    void OnPlayerLogin(Player* player) override
    {
        SendNemesisBootstrap(player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        // Re-send the authoritative snapshot after a map transition. This also
        // gives the client a second deterministic sync point after login/UI load.
        SendNemesisBootstrap(player);
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!IsEligibleNemesisKill(killer, killed))
            return;

        PromoteNemesis(killer, killed);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        ProcessNemesisKillRewards(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        ProcessNemesisKillRewards(owner, killed);
    }
};

class NemesisSystemAllCreatureScript : public AllCreatureScript
{
public:
    NemesisSystemAllCreatureScript() : AllCreatureScript("NemesisSystemAllCreatureScript") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        ApplyNemesisState(creature, state);
        state.homeX = creature->GetPositionX();
        state.homeY = creature->GetPositionY();

        ObjectGuid::LowType const spawnId = creature->GetSpawnId();
        if (!spawnId)
            return;

        {
            std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
            ActiveNemeses[spawnId] = state;
        }

        // A live creature has the authoritative runtime GUID exposed to the WoW client.
        // Push a fresh upsert when it enters the world so target/mouseover matching always
        // uses Creature::GetGUID(), including after server restarts and creature respawns.
        BroadcastNemesisUpsert(spawnId, state);
    }

    void OnAllCreatureUpdate(Creature* creature, uint32 diff) override
    {
        if (!creature)
            return;

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (creature->IsAlive())
        {
            EraseDeadCleanupAccumulator(creature);

            // AllCreatureScript is the reliable per-creature update path. Keep the
            // visual aura self-healing here once per minute so existing/natural Nemeses are covered
            // even when UnitScript::OnUnitUpdate is not dispatched for that creature.
            if (UpdateVisualAuraAccumulator(creature, diff, 60000))
                ApplyNemesisVisualAuras(creature, state.rank);

            return;
        }

        // Player/pet kill rewards need the Nemesis state after the fatal blow.
        // Delay generic dead cleanup briefly so the reward hook cannot race it.
        EraseRegenAccumulator(creature);
        EraseVisualAuraAccumulator(creature);
        if (UpdateDeadCleanupAccumulator(creature, diff, 5000))
            DeleteNemesisState(creature, "dead");
    }
};

class NemesisSystemUnitScript : public UnitScript
{
public:
    NemesisSystemUnitScript() : UnitScript("NemesisSystemUnitScript", true, { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN, UNITHOOK_ON_UNIT_UPDATE, UNITHOOK_ON_UNIT_ENTER_EVADE_MODE, UNITHOOK_ON_UNIT_DEATH }) { }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*evadeReason*/) override
    {
        if (!unit || !unit->IsCreature())
            return;

        Creature* creature = unit->ToCreature();
        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        // AzerothCore removes evade auras and resets the creature before this hook.
        // Re-apply the Nemesis visual here so a promotion caused by killing the last
        // player remains visible immediately after the NPC resets and returns home.
        ApplyNemesisVisualAuras(creature, state.rank);
        EraseVisualAuraAccumulator(creature);
    }

    void OnDamage(Unit* attacker, Unit* /*victim*/, uint32& damage) override
    {
        if (!attacker || !damage || !attacker->IsCreature())
            return;

        Creature* creature = attacker->ToCreature();

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (HasAffix(state, NEMESIS_AFFIX_SAVAGE))
            damage = uint32(float(damage) * GetSavageDamageMultiplier());

        if (HasAffix(state, NEMESIS_AFFIX_ENRAGED) && IsBelowEnrageThreshold(creature))
            damage = uint32(float(damage) * GetEnragedDamageMultiplier());

        if (!HasAffix(state, NEMESIS_AFFIX_VAMPIRIC))
            return;

        uint32 healAmount = std::max<uint32>(1, uint32(float(damage) * GetVampiricHealPct()));
        creature->ModifyHealth(int32(healAmount));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (!target || !attacker || damage <= 0 || !target->IsCreature())
            return;

        Creature* creature = target->ToCreature();

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (!HasAffix(state, NEMESIS_AFFIX_SPELLWARD))
            return;

        damage = std::max<int32>(1, int32(float(damage) * GetSpellwardDamageMultiplier()));
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (!unit || !killer || !unit->IsCreature())
            return;

        Creature* killed = unit->ToCreature();
        if (!killed->GetSpawnId())
            return;

        // Resolve the actual player behind direct kills and controlled units:
        // pets, demons, guardians/charmed units and player-controlled vehicles.
        if (Player* playerKiller = killer->GetCharmerOrOwnerPlayerOrPlayerItself())
            ProcessNemesisKillRewards(playerKiller, killed);
    }

    void OnUnitUpdate(Unit* unit, uint32 diff) override
    {
        if (!unit || !unit->IsCreature())
            return;

        Creature* creature = unit->ToCreature();

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (!HasAffix(state, NEMESIS_AFFIX_REGEN) || !creature->IsAlive() || creature->GetHealth() >= creature->GetMaxHealth())
        {
            EraseRegenAccumulator(creature);
            return;
        }

        uint32 const interval = GetRegenerationIntervalMs();
        if (!UpdateRegenAccumulator(creature, diff, interval))
            return;

        uint32 healAmount = std::max<uint32>(1, uint32(float(creature->GetMaxHealth()) * (GetRegenerationHealthPct() / 100.0f)));
        creature->ModifyHealth(int32(healAmount));
    }
};

class NemesisSystemWorldScript : public WorldScript
{
public:
    NemesisSystemWorldScript() : WorldScript("NemesisSystemWorldScript", { WORLDHOOK_ON_UPDATE }) { }

    void OnUpdate(uint32 diff) override
    {
        uint32 const intervalSeconds = GetAddonSnapshotIntervalSeconds();
        if (!intervalSeconds)
            return;

        uint32 const intervalMs = intervalSeconds * IN_MILLISECONDS;
        _elapsedMs += diff;
        if (_elapsedMs < intervalMs)
            return;

        _elapsedMs %= intervalMs;
        ForEachOnlinePlayer([](Player* player)
        {
            SendNemesisBootstrap(player);
        });
    }

private:
    uint32 _elapsedMs = 0;
};

class NemesisSystemCommandScript : public CommandScript
{
public:
    NemesisSystemCommandScript() : CommandScript("NemesisSystemCommandScript") { }

    ChatCommandTable GetCommands() const override
    {

        static ChatCommandTable nemesisTable =
        {
            { "debug", HandleDebug, SEC_GAMEMASTER, Console::No },
            { "info", HandleInfo, SEC_GAMEMASTER, Console::No },
            { "mark", HandleMark, SEC_GAMEMASTER, Console::No },
            { "reroll", HandleReroll, SEC_GAMEMASTER, Console::No },
            { "list", HandleList, SEC_GAMEMASTER, Console::No },
            { "clear", HandleClear, SEC_GAMEMASTER, Console::No },
            { "mapclear", HandleMapClear, SEC_GAMEMASTER, Console::No },
            { "clearall", HandleClearAll, SEC_ADMINISTRATOR, Console::Yes },
            { "reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "nemesis", nemesisTable }
        };

        return commandTable;
    }

    static bool HandleDebug(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->PSendSysMessage("You must select a creature.");
            return true;
        }

        handler->PSendSysMessage("Nemesis target: {} (entry {}, spawn {}, map {})", target->GetName(), target->GetEntry(), uint64(target->GetSpawnId()), target->GetMapId());

        NemesisState state;
        if (!TryGetNemesisState(target, state))
        {
            handler->PSendSysMessage("Selected creature is not an active nemesis.");
            return true;
        }

        handler->PSendSysMessage("Rank {} | Affixes {} | TargetGuid {}", state.rank, GetAffixList(state.affixMask), state.targetGuid);
        handler->PSendSysMessage("Health {} / {} | Scale {}", target->GetHealth(), target->GetMaxHealth(), target->GetObjectScale());
        handler->PSendSysMessage("Main damage {} - {}", target->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE), target->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE));
        handler->PSendSysMessage("Rank-up cooldown remaining {}s | Same victim cooldown remaining {}s", GetRankUpCooldownRemaining(state), GetSameVictimCooldownRemaining(state, state.targetGuid));
        return true;
    }

    static bool HandleInfo(ChatHandler* handler, uint64 rawSpawnId)
    {
        ObjectGuid::LowType const spawnId = ObjectGuid::LowType(rawSpawnId);

        NemesisState state;
        if (!TryGetNemesisState(spawnId, state))
        {
            handler->PSendSysMessage("Spawn {} is not an active nemesis.", rawSpawnId);
            return true;
        }

        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        Map* map = player ? player->GetMap() : nullptr;
        if (map && map->GetId() != state.mapId)
            map = nullptr;

        Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId);
        std::string name = GetNemesisDisplayName(map, spawnId, state);

        handler->PSendSysMessage("Spawn {} | {} | Entry {} | Map {}", rawSpawnId, name, state.creatureEntry, state.mapId);
        handler->PSendSysMessage("Rank {} | Affixes {} | Target {}", state.rank, GetAffixList(state.affixMask), state.targetGuid);
        handler->PSendSysMessage("Rank-up cooldown remaining {}s | Same victim cooldown remaining {}s", GetRankUpCooldownRemaining(state), GetSameVictimCooldownRemaining(state, state.targetGuid));
        if (liveCreature)
            handler->PSendSysMessage("Loaded now | HP {}/{} | Scale {}", liveCreature->GetHealth(), liveCreature->GetMaxHealth(), liveCreature->GetObjectScale());
        else
            handler->PSendSysMessage("Not currently loaded on your map.");

        return true;
    }

    static bool HandleMark(ChatHandler* handler, Optional<uint8> rankArg)
    {
        Creature* target = handler->getSelectedCreature();
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!target || !player)
        {
            handler->PSendSysMessage("You must select a creature while logged in as a player.");
            return true;
        }

        NemesisState state;
        if (!TryGetNemesisState(target, state))
            state = BuildInitialNemesisState(target, player);

        uint8 rank = state.rank;
        if (rankArg)
            rank = std::clamp<uint8>(*rankArg, 1, GetMaxRank());
        else if (rank < GetMaxRank())
            ++rank;

        state.rank = rank;
        state.mapId = target->GetMapId();
        state.homeX = target->GetPositionX();
        state.homeY = target->GetPositionY();
        state.targetGuid = player->GetGUID().GetCounter();
        RollAffixes(state);
        SaveNemesisState(target, state);
        ApplyNemesisState(target, state);
        target->SetFullHealth();
        if (ObjectGuid::LowType const spawnId = target->GetSpawnId())
            BroadcastNemesisUpsert(spawnId, state);
        handler->PSendSysMessage("Marked {} as nemesis rank {} with affixes {}.", target->GetName(), state.rank, GetAffixList(state.affixMask));
        return true;
    }

    static bool HandleClear(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->PSendSysMessage("You must select a creature.");
            return true;
        }

        NemesisState state;
        if (!TryGetNemesisState(target, state))
        {
            handler->PSendSysMessage("Selected creature is not an active nemesis.");
            return true;
        }

        ResetCreatureToBaseState(target, state);
        DeleteNemesisState(target);
        handler->PSendSysMessage("Cleared nemesis state from {}.", target->GetName());
        return true;
    }

    static bool HandleReroll(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->PSendSysMessage("You must select a creature.");
            return true;
        }

        NemesisState state;
        if (!TryGetNemesisState(target, state))
        {
            handler->PSendSysMessage("Selected creature is not an active nemesis.");
            return true;
        }

        state.affixMask = 0;
        RollAffixes(state);
        SaveNemesisState(target, state);
        ApplyNemesisState(target, state);
        handler->PSendSysMessage("Rerolled affixes for {}: {}.", target->GetName(), GetAffixList(state.affixMask));
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to list map nemeses.");
            return true;
        }

        Map* map = player->GetMap();
        if (!map)
        {
            handler->PSendSysMessage("Unable to resolve your current map.");
            return true;
        }

        uint32 count = 0;
        handler->PSendSysMessage("Active nemeses on map {}:", map->GetId());

        std::vector<std::pair<ObjectGuid::LowType, NemesisState>> persistentNemeses;
        {
            std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);

            persistentNemeses.reserve(ActiveNemeses.size());
            for (auto const& [spawnId, state] : ActiveNemeses)
                if (state.mapId == map->GetId())
                    persistentNemeses.emplace_back(spawnId, state);

        }

        for (auto const& [spawnId, state] : persistentNemeses)
        {
            Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId);
            std::string name = GetNemesisDisplayName(map, spawnId, state);

            handler->PSendSysMessage("Spawn {} | {} | Rank {} | Affixes {} | Target {}{}",
                uint64(spawnId),
                name,
                state.rank,
                GetAffixList(state.affixMask),
                state.targetGuid,
                liveCreature ? Acore::StringFormat(" | HP {}/{}", liveCreature->GetHealth(), liveCreature->GetMaxHealth()) : "");
            ++count;
        }


        if (!count)
            handler->PSendSysMessage("No active nemeses found on this map.");
        else
            handler->PSendSysMessage("Total active nemeses on this map: {}.", count);

        return true;
    }

    static bool HandleMapClear(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to clear map nemeses.");
            return true;
        }

        Map* map = player->GetMap();
        if (!map)
        {
            handler->PSendSysMessage("Unable to resolve your current map.");
            return true;
        }

        std::vector<ObjectGuid::LowType> spawnIds;
        {
            std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);

            spawnIds.reserve(ActiveNemeses.size());
            for (auto const& [spawnId, state] : ActiveNemeses)
                if (state.mapId == map->GetId())
                    spawnIds.push_back(spawnId);

        }

        if (spawnIds.empty())
        {
            handler->PSendSysMessage("No active nemeses found on this map.");
            // Still tell addons to purge stale cached entries for this map.
            BroadcastNemesisMapClear(map->GetId());
            return true;
        }

        for (ObjectGuid::LowType spawnId : spawnIds)
        {
            NemesisState state;
            if (!TryGetNemesisState(spawnId, state))
                continue;

            if (Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId))
                ResetCreatureToBaseState(liveCreature, state);

            DeleteNemesisState(spawnId);
        }


        // REMOVE is sent per persistent Nemesis above. MAP_CLEAR is an
        // Authoritative map-level purge for clients which missed an individual REMOVE packet.
        BroadcastNemesisMapClear(map->GetId());

        handler->PSendSysMessage("Cleared {} active nemesis record(s) from map {}.", spawnIds.size(), map->GetId());
        return true;
    }

    static bool HandleClearAll(ChatHandler* handler)
    {
        EnsureCacheLoaded();
        {
            std::lock_guard<std::recursive_mutex> lock(NemesisStoreMutex);
            ActiveNemeses.clear();
            RegenTickAccumulators.clear();
            VisualAuraTickAccumulators.clear();
            DeadCleanupTickAccumulators.clear();
        }

        CharacterDatabase.Execute("DELETE FROM `character_nemesis`");
        ForEachOnlinePlayer([](Player* player) { SendNemesisBootstrap(player); });
        handler->PSendSysMessage("Cleared all stored nemesis records.");
        return true;
    }
    static bool HandleReload(ChatHandler* handler)
    {
        if (!sConfigMgr->LoadModulesConfigs(true, false))
        {
            handler->PSendSysMessage("Nemesis System configuration reload failed.");
            return true;
        }

        handler->PSendSysMessage("Nemesis System configuration reloaded.");
        return true;
    }
};

void AddSC_mod_nemesis_system()
{
    new NemesisSystemPlayerScript();
    new NemesisSystemWorldScript();
    new NemesisSystemAllCreatureScript();
    new NemesisSystemUnitScript();
    new NemesisSystemCommandScript();
}
