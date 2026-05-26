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
// The server only sends NtfActorMovementData for visible actors.  When an
// actor leaves sight both ActorLinker.position (0x50C) and
// MoveComponent.curPosition (0x28) freeze at the last server value — the SGW
// simulation does NOT continue for OOS actors in this game build.
//
// Strategy:
//   Layer 4a – Hook NtfActorMovementData to cache the last real pos/dir/speed
//              for every actor we see, visible or not.
//   Layer 4b – Hook NtfActorMoveState to track whether each actor is moving.
//   Layer 4c – Hook Interpolation() (per-render-frame visual sync) and, for
//              OOS actors, override myTransform with either:
//              (a) ActorLinker.position if SGW has advanced it, or
//              (b) dead-reckoned position from the cached packet data.
// =============================================================================

struct OOSData {
    float    pos[3];    // last known world position (x,y,z)
    float    fwd[3];    // forward direction (normalised)
    float    speed;     // derived from consecutive packet positions
    bool     isMoving;
    uint64_t lastNs;    // CLOCK_MONOTONIC timestamp of last packet (ns)
};

static std::unordered_map<uint32_t, OOSData> g_oosMap;  // actorID → cached data
static std::unordered_set<uint32_t>          g_oosSet;  // actorIDs currently OOS
static std::mutex                            g_oosMtx;

// Function pointers — set in hack_injec()
static void (*_TransformSetPosInj)(void* transform, float* v3) = nullptr;

static inline void oos_insert(void* inst) {
    if (!inst) return;
    uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4); // ActorLinker.ObjID
    if (!id) return;
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oosSet.insert(id);
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
// Track which actors the server is hiding so Layer 4c can sync them.
// =============================================================================

static void (*_ActorSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
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
// LAYER 3 – SGC::CheckVisible always returns true
// =============================================================================

static bool (*_CheckVisible)(void* attacker, void* target, int32_t flag) = nullptr;
static bool new_CheckVisible(void* attacker, void* target, int32_t flag) {
    if (maphack) return true;
    if (!_CheckVisible) return false;
    return _CheckVisible(attacker, target, flag);
}

// =============================================================================
// LAYER 4a – NtfActorMovementData: cache real position/direction/speed
//
// SGW.DisplayInfoData layout (offsets include IL2CPP value-type header +8):
//   0x08  actorID  (uint32)
//   0x0C  forward  (VInt3: 3× int32, scale 1000 → divide for float)
//   0x18  position (Vector3: 3× float)
//   0x24  groundY  (float)
//
// Speed is derived from distance between consecutive packets so we don't need
// to call get_MoveSpeed() per frame.
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
            if (measured < 20.0f)        // sanity: ≤20 m/s for MOBA hero
                d.speed = measured;
        }
        d.isMoving = (dist > 0.05f);
    }

    d.pos[0] = pos[0]; d.pos[1] = pos[1]; d.pos[2] = pos[2];
    // Convert VInt3 (scale 1000) → normalised float forward
    float fx = fwdInt[0] / 1000.0f;
    float fy = fwdInt[1] / 1000.0f;
    float fz = fwdInt[2] / 1000.0f;
    float mag = sqrtf(fx * fx + fy * fy + fz * fz);
    if (mag > 0.001f) { fx /= mag; fy /= mag; fz /= mag; }
    d.fwd[0] = fx; d.fwd[1] = fy; d.fwd[2] = fz;
    d.lastNs = nowNs;
}

// =============================================================================
// LAYER 4b – NtfActorMoveState: update isMoving flag
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
// Interpolation() is the method that actually writes game logic position into
// myTransform (Unity visual transform).  For OOS actors the game either skips
// this or writes the frozen last-known value, so we must override it here —
// after the original runs — to ensure our write is the final one each frame.
//
// Position selection:
//   1. If ActorLinker.position (0x50C) has moved further from our cached value
//      than a 1-unit threshold, SGW is advancing it → use it directly.
//   2. Otherwise ActorLinker.position is frozen → dead-reckon from cached data:
//      extPos = cachedPos + fwd * speed * dt  (capped at 3 s extrapolation).
//
// try_lock avoids stalling the render thread if another thread holds g_oosMtx.
// =============================================================================

static void (*_Interpolation)(void* inst) = nullptr;
static void new_Interpolation(void* inst) {
    if (_Interpolation) _Interpolation(inst); // run original first

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
        OOSData d = it->second; // copy while locked
        g_oosMtx.unlock();

        float ddx = lpos[0] - d.pos[0];
        float ddz = lpos[2] - d.pos[2];

        if ((ddx * ddx + ddz * ddz) > 1.0f) {
            // ActorLinker.position diverged from cache → SGW is updating it → use directly
            writePos[0] = lpos[0]; writePos[1] = lpos[1]; writePos[2] = lpos[2];
        } else {
            // Both frozen → dead reckon: lastPos + fwd * speed * dt
            writePos[0] = d.pos[0]; writePos[1] = d.pos[1]; writePos[2] = d.pos[2];
            if (d.isMoving && d.speed > 0.05f) {
                float fwdMag = sqrtf(d.fwd[0]*d.fwd[0] + d.fwd[1]*d.fwd[1] + d.fwd[2]*d.fwd[2]);
                if (fwdMag > 0.5f && fwdMag < 2.0f) { // sanity: ~1.0 if normalised correctly
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
        // No cached data yet — use ActorLinker.position as best available
        writePos[0] = lpos[0]; writePos[1] = lpos[1]; writePos[2] = lpos[2];
    }

    _TransformSetPosInj(myTransform, writePos);
}
