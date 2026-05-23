#pragma once
#include <cstdint>
#include <vector>
#include <utility>

// COMDT_HERO_COMMON_INFO field offsets (from dump)
// dwHeroID : 0x8  (uint32)
// wSkinID  : 0x3A (uint16)

bool unlockskin   = false;
bool unlockbutton = false;

int heroid  = 0;
int skinid  = 0;
int heroid2 = 0;
int skinid2 = 0;

namespace CSProtocol {

    class COMDT_HERO_COMMON_INFO {
    public:
        uint32_t getdwHeroID() {
            if (this == nullptr) return 0;
            return *(uint32_t*)((uint64_t)this + 0x8);
        }
        uint16_t getwSkinID() {
            if (this == nullptr) return 0;
            return *(uint16_t*)((uint64_t)this + 0x3A);
        }
        void setdwHeroID(uint32_t v) {
            if (this == nullptr) return;
            *(uint32_t*)((uint64_t)this + 0x8) = v;
        }
        void setwSkinID(uint16_t v) {
            if (this == nullptr) return;
            *(uint16_t*)((uint64_t)this + 0x3A) = v;
        }
    };

    struct saveData {
        static uint32_t heroId;
        static uint16_t skinId;
        static bool     enable;
        static std::vector<std::pair<COMDT_HERO_COMMON_INFO*, uint16_t>> arrayUnpackSkin;

        static void setData(uint32_t hId, uint16_t sId) {
            heroId = hId;
            skinId = sId;
        }
        static void setEnable(bool eb) { enable = eb; }
        static uint32_t  getHeroId()  { return heroId; }
        static uint16_t  getSkinId()  { return skinId; }
        static bool      getEnable()  { return enable; }

        static void resetArrayUnpackSkin() {
            if (!arrayUnpackSkin.empty()) {
                for (const auto& p : arrayUnpackSkin)
                    p.first->setwSkinID(p.second);
                arrayUnpackSkin.clear();
            }
        }
    };

    uint32_t saveData::heroId = 0;
    uint16_t saveData::skinId = 0;
    bool     saveData::enable = false;
    std::vector<std::pair<COMDT_HERO_COMMON_INFO*, uint16_t>> saveData::arrayUnpackSkin;

} // namespace CSProtocol

// ---------------------------------------------------------------------------
// hook_unpack – patch wSkinID after server message is decoded
// ---------------------------------------------------------------------------
static void hook_unpack(CSProtocol::COMDT_HERO_COMMON_INFO* instance) {
    if (!CSProtocol::saveData::enable) return;
    if (instance->getdwHeroID() == CSProtocol::saveData::heroId
        && CSProtocol::saveData::heroId != 0
        && CSProtocol::saveData::skinId != 0)
    {
        CSProtocol::saveData::arrayUnpackSkin.emplace_back(instance, instance->getwSkinID());
        instance->setwSkinID(CSProtocol::saveData::skinId);
    }
}

// COMDT_HERO_COMMON_INFO::unpack(TdrReadBuf& srcBuf, uint32 cutVer) – 0x6F7424C
static int32_t (*_unpack)(void* instance, void* srcBuf, uint32_t cutVer);
static int32_t new_unpack(void* instance, void* srcBuf, uint32_t cutVer) {
    int32_t result = _unpack(instance, srcBuf, cutVer);
    if (unlockskin)
        hook_unpack((CSProtocol::COMDT_HERO_COMMON_INFO*)instance);
    return result;
}

// ---------------------------------------------------------------------------
// CRoleInfo::IsCanUseSkin(heroId, skinId, includeHeroConditions) – 0x4126238
// ---------------------------------------------------------------------------
static bool (*_IsCanUseSkin)(void* instance, uint32_t heroId, uint32_t skinId, bool includeHeroConditions);
static bool new_IsCanUseSkin(void* instance, uint32_t heroId, uint32_t skinId, bool includeHeroConditions) {
    if (unlockskin) {
        if (heroId != 0)
            CSProtocol::saveData::setData(heroId, (uint16_t)skinId);
        return true;
    }
    return _IsCanUseSkin(instance, heroId, skinId, includeHeroConditions);
}

// ---------------------------------------------------------------------------
// CRoleInfo::IsHaveHeroSkin(heroId, skinId, isIncludeLimitSkin,
//                            bCheckHaveCanAcceptForeverGift) – 0x412513C
// ---------------------------------------------------------------------------
static bool (*_IsHaveHeroSkin)(void* instance, uint32_t heroId, uint32_t skinId,
                                bool isIncludeLimitSkin, bool bCheckHaveCanAcceptForeverGift);
static bool new_IsHaveHeroSkin(void* instance, uint32_t heroId, uint32_t skinId,
                                bool isIncludeLimitSkin, bool bCheckHaveCanAcceptForeverGift) {
    if (unlockskin) return true;
    return _IsHaveHeroSkin(instance, heroId, skinId, isIncludeLimitSkin, bCheckHaveCanAcceptForeverGift);
}

// ---------------------------------------------------------------------------
// CSelectHeroFormLogic::GetHeroWearSkinId(heroID) – virtual 0x7DCCCDC
// ---------------------------------------------------------------------------
static uint32_t (*_GetHeroWearSkinId)(void* instance, uint32_t heroID);
static uint32_t new_GetHeroWearSkinId(void* instance, uint32_t heroID) {
    if (unlockskin) {
        CSProtocol::saveData::setEnable(true);
        return CSProtocol::saveData::skinId;
    }
    return _GetHeroWearSkinId(instance, heroID);
}

// ---------------------------------------------------------------------------
// CSelectHeroFormLogic::WearHeroSkin(heroID, skinID) – virtual 0x7DC7A58
// ---------------------------------------------------------------------------
static void (*_WearHeroSkin)(void* instance, uint32_t heroID, uint32_t skinID);
static void new_WearHeroSkin(void* instance, uint32_t heroID, uint32_t skinID) {
    if (unlockskin && instance != nullptr && skinID != 0) {
        CSProtocol::saveData::setData(heroID, (uint16_t)skinID);
        CSProtocol::saveData::setEnable(true);
    }
    _WearHeroSkin(instance, heroID, skinID);
}

// ---------------------------------------------------------------------------
// Helper: register all skin-unlock hooks (call from hack_injec)
// ---------------------------------------------------------------------------
static void RegisterSkinHooks(uintptr_t base) {
    auto addr = [&](uintptr_t off) { return reinterpret_cast<void*>(base + off); };

    DobbyHook(addr(0x6F7424C), (void*)new_unpack,           (void**)&_unpack);
    DobbyHook(addr(0x4126238), (void*)new_IsCanUseSkin,     (void**)&_IsCanUseSkin);
    DobbyHook(addr(0x412513C), (void*)new_IsHaveHeroSkin,   (void**)&_IsHaveHeroSkin);
    DobbyHook(addr(0x7DCCCDC), (void*)new_GetHeroWearSkinId,(void**)&_GetHeroWearSkinId);
    DobbyHook(addr(0x7DC7A58), (void*)new_WearHeroSkin,     (void**)&_WearHeroSkin);
}
