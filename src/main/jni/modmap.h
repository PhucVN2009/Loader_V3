#pragma once
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <time.h>

bool maphack = false;

// =============================================================================
// Out-Of-Sight (OOS) actor tracking
//
// Root-cause analysis for frozen enemy positions:
//
//   When the server decides actor X is OOS for the local player it sends two
//   independent events:
//     A) NtfSetActorVisible(X, false, pos, ...)  →  actor.SetVisible(false,false)
//     B) OnActorLeaveView(X, seq)
//        └─ ActorManager::OnActorLeaveView  ← removes X from ALL iteration lists
//        └─ OnActorLeaveView_UnregisterEvt  ← unsubscribes HP/state event handlers
//
//   Our Layer-2 hook intercepts (A) and forces logicVis=true so the mesh stays
//   rendered.  But (B) still fires, removing X from ActorManager's HeroActors /
//   SoldierActors / ... lists.  Therefore ActorManager::Interpolation() never
//   iterates X → ActorLinker::Interpolation() is NEVER called → our Layer-4c
//   hook never fires → myTransform stays frozen.
//
// Fix strategy:
//   Layer 4c  Interpolation()       – per-render-frame Unity path (if in lists)
//   Layer 4d  HOK_OnInterpolation   – SGW path, called for ALL actors always
//   Layer 7   skip OnActorLeaveView – keep actor in ActorManager render lists
//   Layer 5   OnActorCurHpChange    – force HP update even when "invisible"
//   Layer 6   skip UnregisterEvt    – keep HP/buff event handlers registered
//   Layer 8   ActorMgr::Interpolation + SGW.GetDisplayData()
//             – reads the SGW simulation's live display buffer (all actors,
//               including OOS) once per frame and force-updates their Unity
//               Transforms. Fallback: dead-reckon from last cached packet.
// =============================================================================

struct OOSData {
    float    pos[3];
    float    fwd[3];    // normalised forward direction
    float    speed;     // derived from consecutive packets
    bool     isMoving;
    uint64_t lastNs;    // CLOCK_MONOTONIC ns
};

static std::unordered_map<uint32_t, OOSData> g_oosMap;
static std::unordered_map<uint32_t, void*>   g_actorPtrMap;
static std::unordered_set<uint32_t>          g_oosSet;
static std::mutex                            g_oosMtx;

static void (*_TransformSetPosInj)(void* transform, float* v3) = nullptr;
static void (*_SetActorHp)(void* inst, int32_t curHp, int32_t totalHp) = nullptr;

// SGW.GetDisplayData / GetDisplayData_Count (Scripts.Base.dll, class "SGW")
// Returns a pointer to the raw DisplayInfoData array maintained by the SGW
// simulation engine.  This array is updated for ALL actors in the simulation
// (not just visible ones), so it is the authoritative source of live OOS actor
// positions.
//
// DisplayInfoData IL2CPP layout (stride 0x48 bytes):
//   +0x00  8-byte IL2CPP value-type header
//   +0x08  uint32  actorID
//   +0x0C  VInt3   forward  (3 × int32, scale 1000)
//   +0x18  Vector3 position (3 × float)
//   +0x24  int32   groundY
//   +0x28  Quaternion rotation (4 × float)
//   +0x38  uint32  parentObjID
//   +0x3C  DisplayInfoPredictData predictData
static void*    (*_SGWGetDisplayData)()      = nullptr;
static uint32_t (*_SGWGetDisplayDataCount)() = nullptr;

static constexpr uint32_t k_DispStride    = 0x48;
static constexpr uint32_t k_DispIDOff     = 0x08;
static constexpr uint32_t k_DispPosOff    = 0x18;

// Dead-reckoning time cap: show actor at last-extrapolated position up to
// k_DRCapSec seconds.  Beyond this we still show the last extrapolated spot
// (not the origin) to avoid the model teleporting.
static constexpr float    k_DRCapSec      = 12.0f;

static inline void actor_cache(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_actorPtrMap[id] = inst;
}

// When actor goes OOS: register it and capture current ActorLinker.position as
// the dead-reckoning baseline so the SGW display-buffer read or dead-reckoning
// always starts from a fresh, accurate position.
static inline void oos_insert(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;

    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.insert(id);
    g_actorPtrMap[id] = inst;

    // Snapshot the exact ActorLinker.position at the OOS transition.  This is
    // the most accurate baseline we have – it comes directly from the last
    // server-side position update, not from potentially-stale NtfActorMovementData.
    float* lpos = (float*)((uint64_t)inst + 0x50C); // ActorLinker.position
    OOSData& d = g_oosMap[id];
    d.pos[0] = lpos[0]; d.pos[1] = lpos[1]; d.pos[2] = lpos[2];

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    // Only reset timestamp if we don't already have fresh movement data
    if (d.lastNs == 0 || nowNs - d.lastNs > 2000000000ULL) {
        d.lastNs = nowNs;
    }
}

static inline void oos_remove(uint32_t id) {
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.erase(id);
}

// =============================================================================
// LAYER 1 – FogOfWar
// =============================================================================
static bool (*_FowIsEnable)() = nullptr;
static bool new_FowIsEnable() {
    if (maphack) return false;
    return _FowIsEnable ? _FowIsEnable() : false;
}
static bool (*_FowGetEnable)() = nullptr;
static bool new_FowGetEnable() {
    if (maphack) return false;
    return _FowGetEnable ? _FowGetEnable() : false;
}
static bool (*_FowGetEnableRender)() = nullptr;
static bool new_FowGetEnableRender() {
    if (maphack) return false;
    return _FowGetEnableRender ? _FowGetEnableRender() : false;
}

// =============================================================================
// LAYER 2 – SetVisible / ForceSetVisible
// =============================================================================
static void (*_ActorSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        actor_cache(inst);
        if (!logicVis) oos_insert(inst);
        else if (inst) oos_remove(*(uint32_t*)((uint64_t)inst + 0x4F4));
        logicVis = true; meshVis = true;
    }
    if (_ActorSetVisible) _ActorSetVisible(inst, logicVis, meshVis);
}
static void (*_ActorForceSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorForceSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        actor_cache(inst);
        if (!logicVis) oos_insert(inst);
        else if (inst) oos_remove(*(uint32_t*)((uint64_t)inst + 0x4F4));
        logicVis = true; meshVis = true;
    }
    if (_ActorForceSetVisible) _ActorForceSetVisible(inst, logicVis, meshVis);
}

// =============================================================================
// LAYER 3 – CheckVisible
// =============================================================================
static bool (*_CheckVisible)(void* attacker, void* target, int32_t flag) = nullptr;
static bool new_CheckVisible(void* attacker, void* target, int32_t flag) {
    if (maphack) return true;
    return _CheckVisible ? _CheckVisible(attacker, target, flag) : false;
}

// =============================================================================
// LAYER 4a – NtfActorMovementData: cache pos/fwd/speed
// =============================================================================
static void (*_NtfActorMovementData)(void* dataPtr) = nullptr;
static void new_NtfActorMovementData(void* dataPtr) {
    if (_NtfActorMovementData) _NtfActorMovementData(dataPtr);
    if (!maphack || !dataPtr) return;

    uint32_t actorID = *(uint32_t*)((uint64_t)dataPtr + 0x08);
    if (!actorID) return;
    float*   pos    = (float*)  ((uint64_t)dataPtr + 0x18);
    int32_t* fwdInt = (int32_t*)((uint64_t)dataPtr + 0x0C);

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    std::lock_guard<std::mutex> lk(g_oosMtx);
    OOSData& d = g_oosMap[actorID];
    if (d.lastNs > 0) {
        float dx = pos[0]-d.pos[0], dz = pos[2]-d.pos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        float dt   = (nowNs - d.lastNs) / 1e9f;
        if (dt > 0.001f && dt < 2.0f) {
            float m = dist / dt;
            if (m < 20.0f) d.speed = m;
        }
        d.isMoving = (dist > 0.05f);
    }
    d.pos[0] = pos[0]; d.pos[1] = pos[1]; d.pos[2] = pos[2];
    float fx = fwdInt[0]/1000.0f, fy = fwdInt[1]/1000.0f, fz = fwdInt[2]/1000.0f;
    float mag = sqrtf(fx*fx + fy*fy + fz*fz);
    if (mag > 0.001f) { fx/=mag; fy/=mag; fz/=mag; }
    d.fwd[0]=fx; d.fwd[1]=fy; d.fwd[2]=fz;
    d.lastNs = nowNs;
}

// =============================================================================
// LAYER 4b – NtfActorMoveState
// =============================================================================
static void (*_NtfActorMoveState)(uint32_t actorID, bool isMoving) = nullptr;
static void new_NtfActorMoveState(uint32_t actorID, bool isMoving) {
    if (_NtfActorMoveState) _NtfActorMoveState(actorID, isMoving);
    if (!maphack) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    auto it = g_oosMap.find(actorID);
    if (it != g_oosMap.end()) it->second.isMoving = isMoving;
}

// =============================================================================
// Shared: sync OOS actor myTransform from ActorLinker.position or dead-reckon.
// Called from BOTH Interpolation and HOK_OnInterpolation hooks.
// This is the FALLBACK path – Layer 8 (SGW buffer) is preferred.
// =============================================================================
static inline void sync_oos_transform(void* inst) {
    if (!maphack || !inst) return;
    uint32_t objID = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objID) return;
    if (!g_oosMtx.try_lock()) return;
    bool isOOS = g_oosSet.count(objID) > 0;
    if (!isOOS) { g_oosMtx.unlock(); return; }

    void* myTransform = *(void**)((uint64_t)inst + 0x740);
    if (!myTransform || !_TransformSetPosInj) { g_oosMtx.unlock(); return; }

    float writePos[3];
    float* lpos = (float*)((uint64_t)inst + 0x50C); // ActorLinker.position

    auto it = g_oosMap.find(objID);
    if (it != g_oosMap.end() && it->second.lastNs > 0) {
        OOSData d = it->second;
        g_oosMtx.unlock();

        float ddx = lpos[0]-d.pos[0], ddz = lpos[2]-d.pos[2];
        if ((ddx*ddx + ddz*ddz) > 1.0f) {
            // SGW advanced ActorLinker.position → use it directly
            writePos[0]=lpos[0]; writePos[1]=lpos[1]; writePos[2]=lpos[2];
        } else {
            // Both frozen → dead-reckon from last cached packet
            writePos[0]=d.pos[0]; writePos[1]=d.pos[1]; writePos[2]=d.pos[2];
            if (d.isMoving && d.speed > 0.05f) {
                float fm = sqrtf(d.fwd[0]*d.fwd[0]+d.fwd[1]*d.fwd[1]+d.fwd[2]*d.fwd[2]);
                if (fm > 0.5f && fm < 2.0f) {
                    struct timespec ts2; clock_gettime(CLOCK_MONOTONIC, &ts2);
                    float dt = ((uint64_t)ts2.tv_sec*1000000000ULL+(uint64_t)ts2.tv_nsec - d.lastNs) / 1e9f;
                    if (dt > 0.0f && dt < k_DRCapSec) {
                        writePos[0] += (d.fwd[0]/fm)*d.speed*dt;
                        writePos[1] += (d.fwd[1]/fm)*d.speed*dt;
                        writePos[2] += (d.fwd[2]/fm)*d.speed*dt;
                    }
                }
            }
        }
    } else {
        g_oosMtx.unlock();
        writePos[0]=lpos[0]; writePos[1]=lpos[1]; writePos[2]=lpos[2];
    }
    _TransformSetPosInj(myTransform, writePos);
}

// =============================================================================
// LAYER 4c – Interpolation(): Unity render-loop path
// Called by ActorManager::Interpolation() for actors still in its lists.
// After Layer 7 skips OnActorLeaveView, OOS actors remain in those lists.
// =============================================================================
static void (*_Interpolation)(void* inst) = nullptr;
static void new_Interpolation(void* inst) {
    if (_Interpolation) _Interpolation(inst);
    sync_oos_transform(inst);
}

// =============================================================================
// LAYER 4d – HOK_OnInterpolation(): SGW engine path
// Called by the SGW/HOKExtend system for EVERY actor regardless of ActorManager
// list membership.  This fires even before Layer 7's fix is confirmed working.
// =============================================================================
static void (*_HOKOnInterpolation)(void* inst) = nullptr;
static void new_HOKOnInterpolation(void* inst) {
    if (_HOKOnInterpolation) _HOKOnInterpolation(inst);
    sync_oos_transform(inst);
}

// =============================================================================
// LAYER 5 – HP sync for OOS actors
// SGC::OnActorCurHpChange(uint32 objID, int32 curHp, int32 totalHp) is fired
// by the local SGW simulation for every HP change.  The C# handler checks
// _logicVisible and skips OOS actors; we bypass via cached ActorLinker*.
// =============================================================================
static void (*_OnActorCurHpChange)(uint32_t objID, int32_t curHp, int32_t totalHp) = nullptr;
static void new_OnActorCurHpChange(uint32_t objID, int32_t curHp, int32_t totalHp) {
    if (_OnActorCurHpChange) _OnActorCurHpChange(objID, curHp, totalHp);
    if (!maphack) return;
    if (!g_oosMtx.try_lock()) return;
    bool isOOS = g_oosSet.count(objID) > 0;
    void* actor = nullptr;
    if (isOOS) {
        auto it2 = g_actorPtrMap.find(objID);
        if (it2 != g_actorPtrMap.end()) actor = it2->second;
    }
    g_oosMtx.unlock();
    if (!isOOS || !actor || !_SetActorHp) return;
    void* vc = *(void**)((uint64_t)actor + 0x400); // ValueLinkerComponent*
    if (vc) _SetActorHp(vc, curHp, totalHp);
}

// =============================================================================
// LAYER 6 – Keep HP/buff callbacks alive (skip UnregisterEvt)
// =============================================================================
static void (*_OnActorLeaveViewUnregEvt)(uint32_t actorID) = nullptr;
static void new_OnActorLeaveViewUnregEvt(uint32_t actorID) {
    if (!maphack)
        if (_OnActorLeaveViewUnregEvt) _OnActorLeaveViewUnregEvt(actorID);
    // skipped when maphack: keeps all C# event subscriptions alive
}

// =============================================================================
// LAYER 7 – Skip ActorManager::OnActorLeaveView (instance method)
//
// DIAGNOSIS (confirmed by training-camp observation):
//   SGC::OnActorLeaveView does TWO things in sequence:
//     1. actor.SetVisible(false, false)        ← Layer-2 hook intercepts ✓
//     2. ActorManager.OnActorLeaveView(id,seq) ← removes actor from HeroActors/etc.
//
//   Previous Layer 7 skipped SGC::OnActorLeaveView entirely which also skipped
//   step 1 → actor.SetVisible(false) never called → g_oosSet never populated →
//   sync_oos_transform() returned immediately for every actor → no fix at all.
//
// Correct fix: let SGC::OnActorLeaveView run normally (so SetVisible(false)
// fires → Layer 2 catches it → g_oosSet populated), but skip only the inner
// ActorManager::OnActorLeaveView call so the actor stays in HeroActors/etc.
// render lists → ActorManager::Interpolation() still iterates it every frame →
// Layer 4c fires → sync_oos_transform() runs → position updated.
//
// ActorManager::OnActorLeaveView is an INSTANCE method, so native signature is:
//   void fn(void* actorMgrInst, uint32_t actorID, uint32_t objSeq)
// =============================================================================
static void (*_ActorMgrLeaveView)(void* inst, uint32_t actorID, uint32_t objSeq) = nullptr;
static void new_ActorMgrLeaveView(void* inst, uint32_t actorID, uint32_t objSeq) {
    if (maphack) return; // skip — actor stays in ActorManager render/logic lists
    if (_ActorMgrLeaveView) _ActorMgrLeaveView(inst, actorID, objSeq);
}

// =============================================================================
// LAYER 8 – SGW display-buffer bridge (ActorManager::Interpolation + GetDisplayData)
//
// The SGW C++ simulation runs a full deterministic physics step for ALL actors
// every frame, regardless of the C# visibility layer.  The results are written
// to an internal ring buffer exposed via:
//   SGW.GetDisplayData()       → DisplayInfoData* (array start)
//   SGW.GetDisplayData_Count() → uint32 (element count)
//
// In training/boot-camp mode the server sends movement packets for ALL actors
// so the SGW buffer is always fresh.  In a normal PvP match the buffer entries
// for OOS actors are updated by the LOCAL simulation engine based on the last
// known AI/movement command, so they still advance even when the server has
// stopped sending NtfActorMovementData for those actors.
//
// We hook ActorManager::Interpolation() (the per-frame coordinator) and, after
// the original call finishes updating all visible actors, we scan the SGW buffer
// for OOS actors and force-write their positions directly into the Unity Transform.
//
// If the SGW buffer turns out not to contain live OOS data (e.g. server-mode
// lock-step where the client simulation halts for unseen actors), the fallback
// dead-reckoning in sync_oos_transform() handles those actors via Layers 4c/4d.
// =============================================================================
static inline void sync_oos_from_sgw_buffer() {
    if (!maphack || !_TransformSetPosInj) return;
    if (!_SGWGetDisplayData || !_SGWGetDisplayDataCount) return;

    uint8_t* buf = (uint8_t*)_SGWGetDisplayData();
    uint32_t cnt = _SGWGetDisplayDataCount();
    if (!buf || !cnt) return;

    if (!g_oosMtx.try_lock()) return;
    if (g_oosSet.empty()) { g_oosMtx.unlock(); return; }

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec*1000000000ULL + (uint64_t)ts.tv_nsec;

    for (uint32_t i = 0; i < cnt; i++) {
        uint8_t* e  = buf + (uint64_t)i * k_DispStride;
        uint32_t id = *(uint32_t*)(e + k_DispIDOff);
        if (!id) continue;
        if (!g_oosSet.count(id)) continue;

        auto it = g_actorPtrMap.find(id);
        if (it == g_actorPtrMap.end() || !it->second) continue;
        void* actor = it->second;

        void* xform = *(void**)((uint64_t)actor + 0x740);
        if (!xform) continue;

        float* pos = (float*)(e + k_DispPosOff);

        // Guard against obviously bogus coordinates (NaN / Inf / far-off)
        if (pos[0] != pos[0] || pos[1] != pos[1] || pos[2] != pos[2]) continue; // NaN
        float magSq = pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2];
        if (magSq > 1e8f) continue; // >10000 units from origin → bogus

        // Mirror position into ActorLinker.position so Interpolation() sees it
        float* ap = (float*)((uint64_t)actor + 0x50C);
        ap[0]=pos[0]; ap[1]=pos[1]; ap[2]=pos[2];

        // Force Unity Transform
        _TransformSetPosInj(xform, pos);

        // Keep the dead-reckoning cache current so Layer 4c/4d fallback stays
        // calibrated if we stop getting SGW data for this actor.
        auto oi = g_oosMap.find(id);
        if (oi != g_oosMap.end()) {
            float dx = pos[0]-oi->second.pos[0];
            float dz = pos[2]-oi->second.pos[2];
            float dist = sqrtf(dx*dx+dz*dz);
            float dtSec = (nowNs - oi->second.lastNs) / 1e9f;
            if (dtSec > 0.001f && dtSec < 2.0f) {
                float spd = dist / dtSec;
                if (spd < 20.0f) oi->second.speed = spd;
            }
            oi->second.isMoving = (dist > 0.05f);
            if (dist > 0.01f && dist < 20.0f) {
                float inv = 1.0f / (dist > 0.001f ? dist : 1.0f);
                oi->second.fwd[0] = dx*inv;
                oi->second.fwd[1] = 0.0f;
                oi->second.fwd[2] = dz*inv;
            }
            oi->second.pos[0]=pos[0]; oi->second.pos[1]=pos[1]; oi->second.pos[2]=pos[2];
            oi->second.lastNs=nowNs;
        }
    }
    g_oosMtx.unlock();
}

// Hook on ActorManager::Interpolation() – instance method, native sig:
//   void fn(void* actorMgrInst)
static void (*_ActorMgrInterpolation)(void* inst) = nullptr;
static void new_ActorMgrInterpolation(void* inst) {
    if (_ActorMgrInterpolation) _ActorMgrInterpolation(inst);
    sync_oos_from_sgw_buffer();
}
