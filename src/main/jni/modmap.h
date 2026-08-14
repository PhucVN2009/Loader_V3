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
// Root-cause of frozen / jittery enemy positions:
//
//   A) NtfSetActorVisible(X, false, pos)  →  actor.SetVisible(false)
//   B) ActorManager::OnActorLeaveView     →  removes X from render lists
//   C) OnActorLeaveView_UnregEvt          →  unsubscribes HP callbacks
//   D) Server stops sending NtfActorMovementData for X
//
// Layer overview
// ──────────────
//   Layer 1   FogOfWar hooks         – remove visual fog
//   Layer 2   SetVisible intercept   – force logicVis=true, track OOS set
//   Layer 3   CheckVisible           – always return true
//   Layer 4a  NtfActorMovementData   – cache position/direction from server pkts
//   Layer 4b  NtfActorMoveState      – cache isMoving flag
//   Layer 4c  ActorLinker::Interp()  – PASS-THROUGH only (no extra write)
//   Layer 4d  HOK_OnInterpolation()  – PASS-THROUGH only (no extra write)
//   Layer 5   OnActorCurHpChange     – bypass visibility → push HP update
//   Layer 6   skip UnregEvt          – keep HP/buff callbacks alive
//   Layer 7   skip ActorMgr::OnActorLeaveView – keep actor in render lists
//   Layer 8   ActorManager::Interp   – PRIMARY position driver for OOS actors:
//               1. update positions from SGW.GetDisplayData() (all actors, live)
//               2. for actors absent from SGW buffer: dead-reckon fallback
//               3. smooth lerp to avoid physics-step jitter
//
// Jitter fix (why 4c/4d no longer write to myTransform):
//   HOK_OnInterpolation fires AFTER ActorMgr::Interpolation.  If 4d wrote
//   dead-reckoned positions it would overwrite Layer 8's SGW positions every
//   frame → oscillation.  Layer 8 is the sole OOS position driver; 4c/4d
//   just call through to the original for non-position work (animations etc.)
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

// SGW display-buffer function pointers (Scripts.Base.dll, class "SGW")
// SGW.GetDisplayData()       → raw pointer to DisplayInfoData array (ALL actors)
// SGW.GetDisplayData_Count() → element count
//
// IL2CPP DisplayInfoData stride = 0x50 bytes per element:
//   +0x00  8-byte IL2CPP value-type header
//   +0x08  uint32  actorID
//   +0x0C  VInt3   forward  (3 × int32, scale 1000)
//   +0x18  Vector3 position (3 × float)
//   +0x24  int32   groundY
//   +0x28  Quaternion rotation (4 × float)
//   +0x38  uint32  parentObjID
//   +0x3C  DisplayInfoPredictData predictData
//            +0x3C  Vector3 shadowPosition
//            +0x48  uint32  lerpDiff          ← new field (this update)
//            +0x4C  byte    lerpToLogic_bool  ← new field
//            +0x4D  byte    useShadow_bool    ← new field
//            +0x4E  byte    predictState      ← new field
//            +0x4F  byte    padding
static void*    (*_SGWGetDisplayData)()      = nullptr;
static uint32_t (*_SGWGetDisplayDataCount)() = nullptr;

static constexpr uint32_t k_DispStride = 0x50;
static constexpr uint32_t k_DispIDOff  = 0x08;
static constexpr uint32_t k_DispPosOff = 0x18;

// Dead-reckoning cap: extrapolate at most k_DRCapSec seconds
static constexpr float k_DRCapSec = 12.0f;

// Lerp smoothing alpha for physics-step jitter suppression.
// At 60fps, alpha=0.5 converges to target in ~3 frames; large jumps are
// snapped immediately (see k_SnapDistSq threshold below).
static constexpr float  k_LerpAlpha  = 0.5f;
static constexpr float  k_SnapDistSq = 25.0f; // 5 units – snap, don't lerp

// ─────────────────────────────────────────────────────────────────────────────

static inline void actor_cache(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_actorPtrMap[id] = inst;
}

static inline void oos_insert(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;

    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.insert(id);
    g_actorPtrMap[id] = inst;

    // Capture exact ActorLinker.position at OOS transition as dead-reckon baseline
    float* lpos = (float*)((uint64_t)inst + 0x50C);
    OOSData& d = g_oosMap[id];
    d.pos[0] = lpos[0]; d.pos[1] = lpos[1]; d.pos[2] = lpos[2];

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    if (d.lastNs == 0 || nowNs - d.lastNs > 2000000000ULL)
        d.lastNs = nowNs;
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
// LAYER 4c – ActorLinker::Interpolation(): PASS-THROUGH
// DO NOT write to myTransform here – Layer 8 is the sole OOS position driver.
// Calling the original is still important for non-position work (animations,
// HUD, bone sync, etc.) so we never skip it.
// =============================================================================
static void (*_Interpolation)(void* inst) = nullptr;
static void new_Interpolation(void* inst) {
    if (_Interpolation) _Interpolation(inst);
    // OOS position is handled by Layer 8 (ActorMgrInterpolation).
    // No sync_oos_transform here – that was causing jitter by fighting with Layer 8.
}

// =============================================================================
// LAYER 4d – HOK_OnInterpolation(): PASS-THROUGH
// HOK fires AFTER ActorManager::Interpolation().  Writing dead-reckoned
// positions here would overwrite Layer 8's fresh SGW positions every frame.
// =============================================================================
static void (*_HOKOnInterpolation)(void* inst) = nullptr;
static void new_HOKOnInterpolation(void* inst) {
    if (_HOKOnInterpolation) _HOKOnInterpolation(inst);
    // OOS position handled by Layer 8 – no sync here.
}

// =============================================================================
// LAYER 5 – HP sync for OOS actors
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
    void* vc = *(void**)((uint64_t)actor + 0x400);
    if (vc) _SetActorHp(vc, curHp, totalHp);
}

// =============================================================================
// LAYER 6 – Keep HP/buff callbacks alive (skip UnregisterEvt)
// =============================================================================
static void (*_OnActorLeaveViewUnregEvt)(uint32_t actorID) = nullptr;
static void new_OnActorLeaveViewUnregEvt(uint32_t actorID) {
    if (!maphack)
        if (_OnActorLeaveViewUnregEvt) _OnActorLeaveViewUnregEvt(actorID);
}

// =============================================================================
// LAYER 7 – Skip ActorManager::OnActorLeaveView
// Keeps actor in HeroActors/SoldierActors so Interpolation() still iterates it.
// =============================================================================
static void (*_ActorMgrLeaveView)(void* inst, uint32_t actorID, uint32_t objSeq) = nullptr;
static void new_ActorMgrLeaveView(void* inst, uint32_t actorID, uint32_t objSeq) {
    if (maphack) return;
    if (_ActorMgrLeaveView) _ActorMgrLeaveView(inst, actorID, objSeq);
}

// =============================================================================
// LAYER 8 – SGW display-buffer + dead-reckoning (sole OOS position driver)
//
// Called from new_ActorMgrInterpolation BEFORE the original so that
// ActorLinker.position is already fresh when game's Interpolation() runs.
// Then after the original we do a second pass for actors not yet updated
// (safety net – usually empty).
//
// Smoothing strategy:
//   • If |delta| > k_SnapDistSq  → snap immediately (fresh spawn / large jump)
//   • Otherwise                  → lerp with alpha k_LerpAlpha per frame
//     This suppresses 30Hz physics-step snapping that would otherwise be
//     visible at 60fps.
// =============================================================================
static inline void sync_oos_from_sgw_buffer(bool postPass) {
    if (!maphack || !_TransformSetPosInj) return;

    uint8_t* buf = nullptr;
    uint32_t cnt = 0;
    if (_SGWGetDisplayData && _SGWGetDisplayDataCount) {
        buf = (uint8_t*)_SGWGetDisplayData();
        cnt = _SGWGetDisplayDataCount();
    }

    if (!g_oosMtx.try_lock()) return;
    if (g_oosSet.empty()) { g_oosMtx.unlock(); return; }

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec*1000000000ULL + (uint64_t)ts.tv_nsec;

    // Track which OOS actors we handle from the SGW buffer (pre-pass only)
    // so the dead-reckoning fallback only fires for those not in the buffer.
    static std::unordered_set<uint32_t> s_handled;
    if (!postPass) s_handled.clear();

    // ── A: SGW buffer pass ────────────────────────────────────────────────────
    if (buf && cnt && !postPass) {
        for (uint32_t i = 0; i < cnt; i++) {
            uint8_t* e  = buf + (uint64_t)i * k_DispStride;
            uint32_t id = *(uint32_t*)(e + k_DispIDOff);
            if (!id || !g_oosSet.count(id)) continue;

            auto it = g_actorPtrMap.find(id);
            if (it == g_actorPtrMap.end() || !it->second) continue;
            void* actor = it->second;

            void* xform = *(void**)((uint64_t)actor + 0x740);
            if (!xform) continue;

            float* tgt = (float*)(e + k_DispPosOff);

            // Sanity: reject NaN or impossibly far positions
            if (tgt[0]!=tgt[0] || tgt[1]!=tgt[1] || tgt[2]!=tgt[2]) continue;
            if (tgt[0]*tgt[0]+tgt[2]*tgt[2] > 1e8f) continue;

            // Current smoothed position (from ActorLinker or our last write)
            float* ap = (float*)((uint64_t)actor + 0x50C);
            float dx=tgt[0]-ap[0], dy=tgt[1]-ap[1], dz=tgt[2]-ap[2];
            float distSq = dx*dx+dy*dy+dz*dz;

            float sp[3];
            if (distSq > k_SnapDistSq) {
                // Large jump → snap (new actor or teleport)
                sp[0]=tgt[0]; sp[1]=tgt[1]; sp[2]=tgt[2];
            } else {
                // Small delta → smooth lerp to suppress physics-step jitter
                sp[0]=ap[0]+k_LerpAlpha*dx;
                sp[1]=ap[1]+k_LerpAlpha*dy;
                sp[2]=ap[2]+k_LerpAlpha*dz;
            }

            // Write back to ActorLinker.position (input to game's Interpolation)
            // and directly to Unity Transform (safety – in case game's Interpolation
            // uses a different source for OOS actors).
            ap[0]=sp[0]; ap[1]=sp[1]; ap[2]=sp[2];
            _TransformSetPosInj(xform, sp);

            // Refresh dead-reckoning cache so the fallback (below) starts accurate
            auto oi = g_oosMap.find(id);
            if (oi != g_oosMap.end()) {
                float ddx=sp[0]-oi->second.pos[0], ddz=sp[2]-oi->second.pos[2];
                float dist=sqrtf(ddx*ddx+ddz*ddz);
                float dtSec=(nowNs-oi->second.lastNs)/1e9f;
                if (dtSec>0.001f && dtSec<2.0f) {
                    float spd=dist/dtSec;
                    if (spd<20.0f) oi->second.speed=spd;
                }
                oi->second.isMoving=(dist>0.05f);
                if (dist>0.01f && dist<20.0f) {
                    float inv=1.0f/(dist>0.001f?dist:1.0f);
                    oi->second.fwd[0]=ddx*inv;
                    oi->second.fwd[1]=0.0f;
                    oi->second.fwd[2]=ddz*inv;
                }
                oi->second.pos[0]=sp[0]; oi->second.pos[1]=sp[1]; oi->second.pos[2]=sp[2];
                oi->second.lastNs=nowNs;
            }

            s_handled.insert(id);
        }
    }

    // ── B: dead-reckoning fallback for OOS actors absent from SGW buffer ─────
    // (Also the sole path when SGW buffer is unavailable or empty)
    for (uint32_t id : g_oosSet) {
        if (!postPass && s_handled.count(id)) continue; // already handled above

        auto pit = g_actorPtrMap.find(id);
        if (pit == g_actorPtrMap.end() || !pit->second) continue;
        void* actor = pit->second;

        void* xform = *(void**)((uint64_t)actor + 0x740);
        if (!xform) continue;

        float* ap = (float*)((uint64_t)actor + 0x50C);

        auto oi = g_oosMap.find(id);
        float wp[3];
        if (oi != g_oosMap.end() && oi->second.lastNs > 0) {
            OOSData& d = oi->second;

            // Dead-reckon from last known position + forward * speed * dt
            wp[0]=d.pos[0]; wp[1]=d.pos[1]; wp[2]=d.pos[2];
            if (d.isMoving && d.speed > 0.05f) {
                float fm=sqrtf(d.fwd[0]*d.fwd[0]+d.fwd[1]*d.fwd[1]+d.fwd[2]*d.fwd[2]);
                if (fm>0.5f && fm<2.0f) {
                    float dt2=(nowNs-d.lastNs)/1e9f;
                    if (dt2>0.0f && dt2<k_DRCapSec) {
                        wp[0]+=(d.fwd[0]/fm)*d.speed*dt2;
                        wp[1]+=(d.fwd[1]/fm)*d.speed*dt2;
                        wp[2]+=(d.fwd[2]/fm)*d.speed*dt2;
                    }
                }
            }
        } else {
            wp[0]=ap[0]; wp[1]=ap[1]; wp[2]=ap[2];
        }

        ap[0]=wp[0]; ap[1]=wp[1]; ap[2]=wp[2];
        _TransformSetPosInj(xform, wp);
    }

    g_oosMtx.unlock();
}

// Hook on ActorManager::Interpolation() – instance method (void* inst)
// Pre-pass: update ActorLinker.position so game's own Interpolation() reads
// fresh data when it runs for OOS actors still in its lists.
// Post-pass: force-update Transform for actors not reached by game's path.
static void (*_ActorMgrInterpolation)(void* inst) = nullptr;
static void new_ActorMgrInterpolation(void* inst) {
    // Pre-pass: populate ActorLinker.position and myTransform before game iterates
    sync_oos_from_sgw_buffer(false);

    if (_ActorMgrInterpolation) _ActorMgrInterpolation(inst);

    // Post-pass: any OOS actor whose Transform was overwritten by the game's
    // stale-position Interpolation() gets corrected here.
    sync_oos_from_sgw_buffer(true);
}
