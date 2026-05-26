#pragma once
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <mutex>
#include <time.h>

bool maphack = false;

// =============================================================================
// Dead-reckoning cache: actors whose visibility was server-hidden but we forced
// them visible. We extrapolate their position from the last known state.
// =============================================================================

struct OOSActor {           // Out-Of-Sight actor snapshot
    float    pos[3];        // last known Unity-world position (Vector3)
    float    fwdX, fwdZ;    // normalised forward direction (XZ plane)
    float    speed;         // movement speed m/s
    bool     isMoving;      // last NtfActorMoveState state
    uint64_t lastNs;        // CLOCK_MONOTONIC timestamp (nanoseconds)
};

static std::unordered_map<uint32_t, OOSActor> g_oos;   // actorID → state
static std::mutex                              g_oosMtx;

// Set in hack_injec() after Il2CppGetMethodOffset lookups
static void (*_TransformSetPosInj)(void* transform, float* v3) = nullptr;
static void (*_ActorUpdatePosNoArg)(void* inst)                = nullptr;

static inline uint64_t monoNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Called when an actor is forced invisible (SetVisible/ForceSetVisible → false).
// Saves the last known position and facing direction for later extrapolation.
static inline void oos_insert(void* inst) {
    if (!inst) return;
    uint32_t objID = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objID) return;

    const float* pos  = (const float*)  ((uint64_t)inst + 0x50C); // Vector3 position
    const int32_t* vf = (const int32_t*)((uint64_t)inst + 0x500); // VInt3  forward

    // VInt3 scale is 1000 (fixed-point). Convert to float unit vector (XZ only).
    float fx = (float)vf[0] / 1000.0f;
    float fz = (float)vf[2] / 1000.0f;
    float fl = sqrtf(fx * fx + fz * fz);
    if (fl > 0.001f) { fx /= fl; fz /= fl; }
    else             { fx = 0.0f; fz = 0.0f; }

    OOSActor e;
    e.pos[0]   = pos[0]; e.pos[1] = pos[1]; e.pos[2] = pos[2];
    e.fwdX     = fx;     e.fwdZ   = fz;
    e.speed    = 6.5f;   // typical HoK hero speed (m/s); refined via NtfActorMoveState
    e.isMoving = false;
    e.lastNs   = monoNs();

    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oos[objID] = e;
}

// Called when an actor becomes visible again; removes it from extrapolation.
static inline void oos_remove(uint32_t objID) {
    std::lock_guard<std::mutex> lk(g_oosMtx);
    g_oos.erase(objID);
}

// =============================================================================
// LAYER 1 – FogOfWar rendering / logic disabled
// Class FogOfWar (Scripts.GameCore.dll, global namespace)
// Static methods, 0 args. Disables the visual fog overlay and FOW system.
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
// LAYER 2 – ActorLinker::SetVisible / ForceSetVisible (force always visible)
// Class ActorLinker (Scripts.GameCore.dll, Assets.Scripts.GameLogic)
// Instance methods, 2 args: (bool bLogicVisible, bool bMeshVisible)
//
// Flow: server sends NtfSetActorVisible(id, visible=false)
//       → SGC::NtfSetActorVisible → ActorLinker::SetVisible(false, false)
//       → ForceSetVisible(false, false)  ← sets _logicVisible(0x559) / _meshVisible(0x55A)
//
// By forcing (true, true) at both points, enemy actors stay visible at their
// last-known position after leaving our sight range ("ghost tracking").
//
// When visibility is REMOVED (logicVis=false), we save the actor's last known
// position/forward for dead-reckoning (Layer 4).
// When visibility is RESTORED (logicVis=true), we remove the cached entry.
// =============================================================================

static void (*_ActorSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        if (!logicVis) oos_insert(inst);    // going invisible – snapshot position
        else {
            if (inst) {
                uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
                if (id) oos_remove(id);     // coming back visible – stop extrapolation
            }
        }
        logicVis = true; meshVis = true;
    }
    if (_ActorSetVisible) _ActorSetVisible(inst, logicVis, meshVis);
}

static void (*_ActorForceSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorForceSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) {
        if (!logicVis) oos_insert(inst);
        else {
            if (inst) {
                uint32_t id = *(uint32_t*)((uint64_t)inst + 0x4F4);
                if (id) oos_remove(id);
            }
        }
        logicVis = true; meshVis = true;
    }
    if (_ActorForceSetVisible) _ActorForceSetVisible(inst, logicVis, meshVis);
}

// =============================================================================
// LAYER 3 – SGC::CheckVisible (visibility queries always return true)
// Class SGC (Scripts.GameCore.dll, global namespace)
// Static method, 3 args: (ActorLinker* attacker, ActorLinker* target, int32_t flag)
//
// Used by skills, AI, minimap icon rendering to ask "can A see B?".
// Returning true makes the game think all units are always visible to us.
// =============================================================================

static bool (*_CheckVisible)(void* attacker, void* target, int32_t flag) = nullptr;
static bool new_CheckVisible(void* attacker, void* target, int32_t flag) {
    if (maphack) return true;
    if (!_CheckVisible) return false;
    return _CheckVisible(attacker, target, flag);
}

// =============================================================================
// LAYER 4 – Dead-reckoning position extrapolation for out-of-sight actors
//
// Problem: the server stops sending NtfActorMovementData for actors outside
// our sight range. The actor model stays visible (Layer 2) but frozen at its
// last-known position even while animations play.
//
// Solution:
//   a) SGC::NtfActorMoveState hook – tracks whether each actor is walking.
//   b) ActorLinker::HOK_OnLateUpdate hook – runs every frame per actor.
//      For actors in the OOS cache: extrapolate position forward by dt seconds
//      in the last-known facing direction, write the result into both the
//      ActorLinker.position field (0x50C) and directly into the Unity Transform
//      via Transform::set_position_Injected so the mesh actually moves.
//
// Accuracy note: direction is last known facing; speed defaults to 6.5 m/s.
// Extrapolation is capped at 5 s to avoid absurd ghost positions.
// =============================================================================

// SGC::NtfActorMoveState(uint32 objID, bool bMove) – static, 2 args
static void (*_NtfActorMoveState)(uint32_t objID, bool bMove) = nullptr;
static void new_NtfActorMoveState(uint32_t objID, bool bMove) {
    if (maphack) {
        std::lock_guard<std::mutex> lk(g_oosMtx);
        auto it = g_oos.find(objID);
        if (it != g_oos.end())
            it->second.isMoving = bMove;
    }
    if (_NtfActorMoveState) _NtfActorMoveState(objID, bMove);
}

// ActorLinker::HOK_OnLateUpdate(int32 nDelta) – instance, 1 arg
static void (*_HOKLateUpdate)(void* inst, int32_t nDelta) = nullptr;
static void new_HOKLateUpdate(void* inst, int32_t nDelta) {
    // Run original first (may update position for visible actors from SGW data)
    if (_HOKLateUpdate) _HOKLateUpdate(inst, nDelta);

    if (!maphack || !inst) return;

    uint32_t objID = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objID) return;

    g_oosMtx.lock();
    auto it = g_oos.find(objID);
    if (it == g_oos.end()) { g_oosMtx.unlock(); return; }
    OOSActor dat = it->second;   // copy so we can release lock fast
    g_oosMtx.unlock();

    // Time since we last had real server data for this actor
    float dt = (float)((double)(monoNs() - dat.lastNs) / 1.0e9);
    if (dt > 5.0f) return;  // stop extrapolating after 5 seconds

    // Extrapolate position in last known forward direction
    float ex = dat.pos[0], ey = dat.pos[1], ez = dat.pos[2];
    if (dat.isMoving && (dat.fwdX != 0.0f || dat.fwdZ != 0.0f)) {
        ex += dat.fwdX * dat.speed * dt;
        ez += dat.fwdZ * dat.speed * dt;
    }

    // 1. Write into ActorLinker.position (logical position, 0x50C = Vector3)
    float* lpos = (float*)((uint64_t)inst + 0x50C);
    lpos[0] = ex;  lpos[1] = ey;  lpos[2] = ez;

    // 2. Optionally call UpdatePosition() no-arg to trigger internal sync
    if (_ActorUpdatePosNoArg)
        _ActorUpdatePosNoArg(inst);

    // 3. Drive the Unity Transform directly so the mesh actually moves
    void* myTransform = *(void**)((uint64_t)inst + 0x740);
    if (myTransform && _TransformSetPosInj) {
        float v3[3] = { ex, ey, ez };
        _TransformSetPosInj(myTransform, v3);
    }
}
