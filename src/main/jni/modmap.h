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
// The server only sends NtfActorMovementData / OnActorCurHpChange through the
// client's own visibility window.  When an actor leaves sight:
//   • SGC::OnActorLeaveView_UnregisterEvt() unsubscribes HP/state callbacks
//   • SGW still fires OnActorCurHpChange from the local simulation for ALL
//     actors, but the C# handler checks visibility and skips OOS actors
//
// Layers implemented here:
//   1  FogOfWar disabled (FOW rendering)
//   2  SetVisible / ForceSetVisible forced true
//   3  CheckVisible always true
//   4a NtfActorMovementData – cache pos / fwd / speed for dead reckoning
//   4b NtfActorMoveState    – cache isMoving flag
//   4c Interpolation()      – push cached/dead-reckoned pos into myTransform
//   5  OnActorCurHpChange   – force SetActorHp even when OOS
//   6  OnActorLeaveView_UnregisterEvt – skip so HP callbacks stay registered
// =============================================================================

struct OOSData {
    float    pos[3];    // last known world position
    float    fwd[3];    // forward direction (normalised)
    float    speed;     // derived from consecutive packet positions
    bool     isMoving;
    uint64_t lastNs;    // CLOCK_MONOTONIC timestamp of last packet (ns)
};

static std::unordered_map<uint32_t, OOSData> g_oosMap;     // actorID → cached movement data
static std::unordered_map<uint32_t, void*>   g_actorPtrMap; // actorID → ActorLinker*
static std::unordered_set<uint32_t>          g_oosSet;      // actorIDs currently OOS
static std::mutex                            g_oosMtx;

// Function pointers — set in hack_injec()
static void (*_TransformSetPosInj)(void* transform, float* v3) = nullptr;
static void (*_SetActorHp)(void* inst, int32_t curHp, int32_t totalHp) = nullptr;

// Cache the ActorLinker* for every actor we observe in any SetVisible call.
// Required by Layer 5 to look up the actor by objID without managed runtime calls.
static inline void actor_cache(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    // Reuse g_oosMtx – caller must NOT already hold it.
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_actorPtrMap[id] = inst;
}

static inline void oos_insert(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.insert(id);
    g_actorPtrMap[id] = inst; // keep ptr current
}
static inline void oos_remove(uint32_t id) {
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.erase(id);
}

// =============================================================================
// LAYER 1 – FogOfWar rendering / logic disabled
// =============================================================================

static bool (*_FowIsEnable)() = nullptr;
static bool new_FowIsEnable() {
    if (maphack) return false;
    if (!_FowIsEnable) return false;
    return _FowIsEnable();
}

static bool (*_FowGetEnable)() = nullptr;
static bool new_FowGetEnable() {
    if (maphack) return false;
    if (!_FowGetEnable) return false;
    return _FowGetEnable();
}

static bool (*_FowGetEnableRender)() = nullptr;
static bool new_FowGetEnableRender() {
    if (maphack) return false;
    if (!_FowGetEnableRender) return false;
    return _FowGetEnableRender();
}

// =============================================================================
// LAYER 2 – ActorLinker::SetVisible / ForceSetVisible
// =============================================================================

static void (*_ActorSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        actor_cache(inst); // always keep pointer current
        if (!logicVis) oos_insert(inst);
        else if (inst) {
            uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
            oos_remove(id);
        }
        logicVis = true; meshVis = true;
    }
    if (_ActorSetVisible) _ActorSetVisible(inst, logicVis, meshVis);
}

static void (*_ActorForceSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorForceSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        actor_cache(inst);
        if (!logicVis) oos_insert(inst);
        else if (inst) {
            uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
            oos_remove(id);
        }
        logicVis = true; meshVis = true;
    }
    if (_ActorForceSetVisible) _ActorForceSetVisible(inst, logicVis, meshVis);
}

// =============================================================================
// LAYER 3 – SGC::CheckVisible always true
// =============================================================================

static bool (*_CheckVisible)(void* attacker, void* target, int32_t flag) = nullptr;
static bool new_CheckVisible(void* attacker, void* target, int32_t flag) {
    if (maphack) return true;
    if (!_CheckVisible) return false;
    return _CheckVisible(attacker, target, flag);
}

// =============================================================================
// LAYER 4a – NtfActorMovementData: cache real position / direction / speed
//
// SGW.DisplayInfoData layout (offsets include IL2CPP value-type header +8):
//   0x08  actorID  (uint32)
//   0x0C  forward  (VInt3: 3× int32, scale 1000)
//   0x18  position (Vector3: 3× float)
// =============================================================================

static void (*_NtfActorMovementData)(void* dataPtr) = nullptr;
static void new_NtfActorMovementData(void* dataPtr) {
    if (_NtfActorMovementData) _NtfActorMovementData(dataPtr);
    if (!maphack || !dataPtr) return;

    uint32_t actorID = *(uint32_t*)((uint64_t)dataPtr + 0x08);
    if (!actorID) return;

    float*   pos    = (float*)  ((uint64_t)dataPtr + 0x18);
    int32_t* fwdInt = (int32_t*)((uint64_t)dataPtr + 0x0C);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nowNs = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    std::lock_guard<std::mutex> lk(g_oosMtx);
    OOSData& d = g_oosMap[actorID];

    if (d.lastNs > 0) {
        float dx   = pos[0] - d.pos[0];
        float dz   = pos[2] - d.pos[2];
        float dist = sqrtf(dx * dx + dz * dz);
        float dt   = (nowNs - d.lastNs) / 1e9f;
        if (dt > 0.001f && dt < 2.0f) {
            float measured = dist / dt;
            if (measured < 20.0f)
                d.speed = measured;
        }
        d.isMoving = (dist > 0.05f);
    }

    d.pos[0] = pos[0]; d.pos[1] = pos[1]; d.pos[2] = pos[2];
    float fx = fwdInt[0] / 1000.0f;
    float fy = fwdInt[1] / 1000.0f;
    float fz = fwdInt[2] / 1000.0f;
    float mag = sqrtf(fx * fx + fy * fy + fz * fz);
    if (mag > 0.001f) { fx /= mag; fy /= mag; fz /= mag; }
    d.fwd[0] = fx; d.fwd[1] = fy; d.fwd[2] = fz;
    d.lastNs = nowNs;
}

// =============================================================================
// LAYER 4b – NtfActorMoveState: cache isMoving flag
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
// LAYER 4c – Interpolation(): per-render-frame visual sync
//
// Override myTransform after the original runs.  For OOS actors:
//   (a) If ActorLinker.position (0x50C) diverged >1 unit from cached value,
//       SGW is advancing it → use it directly.
//   (b) Otherwise dead-reckon: cachedPos + fwd * speed * dt (≤3 s).
// try_lock avoids stalling the render thread.
// =============================================================================

static void (*_Interpolation)(void* inst) = nullptr;
static void new_Interpolation(void* inst) {
    if (_Interpolation) _Interpolation(inst);

    if (!maphack || !inst) return;
    uint32_t objID = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objID) return;

    if (!g_oosMtx.try_lock()) return;
    bool isOOS = g_oosSet.count(objID) > 0;
    if (!isOOS) { g_oosMtx.unlock(); return; }

    void* myTransform = *(void**)((uint64_t)inst + 0x740);
    if (!myTransform || !_TransformSetPosInj) { g_oosMtx.unlock(); return; }

    float writePos[3];
    float* lpos = (float*)((uint64_t)inst + 0x50C);

    auto it = g_oosMap.find(objID);
    if (it != g_oosMap.end() && it->second.lastNs > 0) {
        OOSData d = it->second;
        g_oosMtx.unlock();

        float ddx = lpos[0] - d.pos[0];
        float ddz = lpos[2] - d.pos[2];

        if ((ddx * ddx + ddz * ddz) > 1.0f) {
            writePos[0] = lpos[0]; writePos[1] = lpos[1]; writePos[2] = lpos[2];
        } else {
            writePos[0] = d.pos[0]; writePos[1] = d.pos[1]; writePos[2] = d.pos[2];
            if (d.isMoving && d.speed > 0.05f) {
                float fwdMag = sqrtf(d.fwd[0]*d.fwd[0] + d.fwd[1]*d.fwd[1] + d.fwd[2]*d.fwd[2]);
                if (fwdMag > 0.5f && fwdMag < 2.0f) {
                    struct timespec ts2;
                    clock_gettime(CLOCK_MONOTONIC, &ts2);
                    uint64_t nowNs = (uint64_t)ts2.tv_sec * 1000000000ULL + (uint64_t)ts2.tv_nsec;
                    float dt = (nowNs - d.lastNs) / 1e9f;
                    if (dt > 0.0f && dt < 3.0f) {
                        writePos[0] += (d.fwd[0] / fwdMag) * d.speed * dt;
                        writePos[1] += (d.fwd[1] / fwdMag) * d.speed * dt;
                        writePos[2] += (d.fwd[2] / fwdMag) * d.speed * dt;
                    }
                }
            }
        }
    } else {
        g_oosMtx.unlock();
        writePos[0] = lpos[0]; writePos[1] = lpos[1]; writePos[2] = lpos[2];
    }

    _TransformSetPosInj(myTransform, writePos);
}

// =============================================================================
// LAYER 5 – HP sync for OOS actors
//
// SGC::OnActorCurHpChange(uint32 objID, int32 curHp, int32 totalHp) is a
// static C# method called from the SGW C++ simulation for every HP change,
// including OOS actors.  The original handler checks actor visibility before
// calling ValueLinkerComponent::SetActorHp, so OOS actors are silently skipped.
//
// We call the original first (handles visible actors), then for any actor in
// g_oosSet we directly invoke SetActorHp via the cached ActorLinker pointer.
//
// ActorLinker layout:
//   0x400  ValueComponent (ValueLinkerComponent*)
// ValueLinkerComponent layout:
//   0x38   actorHp  (int32)
//   0x3C   actorHpTotal (int32)
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
    if (!vc) return;

    _SetActorHp(vc, curHp, totalHp);
}

// =============================================================================
// LAYER 6 – Prevent HP callback unregistration when actor goes OOS
//
// SGC::OnActorLeaveView_UnregisterEvt(uint32 actorID) unsubscribes all C#
// event handlers (including HP change, buff events) for the given actor.
// Skipping this call when maphack is on keeps the handlers active so that
// any HP-change events fired by the SGW simulation still reach the HP bar UI.
// =============================================================================

static void (*_OnActorLeaveViewUnregEvt)(uint32_t actorID) = nullptr;
static void new_OnActorLeaveViewUnregEvt(uint32_t actorID) {
    if (!maphack) {
        if (_OnActorLeaveViewUnregEvt) _OnActorLeaveViewUnregEvt(actorID);
    }
    // When maphack is on: intentionally skipped — keeps all event subscriptions active.
}
