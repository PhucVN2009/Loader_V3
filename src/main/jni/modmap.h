#pragma once
#include <cstdint>
#include <unordered_set>
#include <mutex>

bool maphack = false;

// =============================================================================
// Out-Of-Sight (OOS) actor set
//
// The game's SGW simulation continues to update ActorLinker.position (0x50C)
// for ALL actors even when they leave sight — skill-effect positions prove this.
// The only missing step is UpdateMoveComponent(), which syncs the logic position
// to the Unity Transform (myTransform at 0x740).  That call is gated on the
// server's visibility state; we bypass it by calling it ourselves each frame.
// =============================================================================

static std::unordered_set<uint32_t> g_oosSet;
static std::mutex                   g_oosMtx;

// Function pointers set in hack_injec() — see Main.cpp
// ActorLinker::UpdateMoveComponent() – private, but findable at runtime
static void (*_UpdateMoveComp)(void* inst) = nullptr;
// UnityEngine.Transform::set_position_Injected(ref Vector3) – direct fallback
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
// Class FogOfWar (Scripts.GameCore.dll, global namespace)
// Static methods, 0 args.
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
// Class ActorLinker (Scripts.GameCore.dll, Assets.Scripts.GameLogic)
// Instance methods, 2 args: (bool bLogicVisible, bool bMeshVisible)
//
// When logicVisible goes false  → record actor in OOS set so Layer 4 can sync.
// When logicVisible goes true   → remove from OOS set (server data resumes).
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
// LAYER 3 – SGC::CheckVisible (visibility queries always return true)
// Class SGC (Scripts.GameCore.dll, global namespace)
// Static method, 3 args: (ActorLinker* attacker, ActorLinker* target, int32 flag)
// =============================================================================

static bool (*_CheckVisible)(void* attacker, void* target, int32_t flag) = nullptr;
static bool new_CheckVisible(void* attacker, void* target, int32_t flag) {
    if (maphack) return true;
    if (!_CheckVisible) return false;
    return _CheckVisible(attacker, target, flag);
}

// =============================================================================
// LAYER 4 – Transform sync for out-of-sight actors
//
// The SGW simulation keeps ActorLinker.position (0x50C) correct for ALL actors.
// Skill effects confirm this: they appear at the real current position.
// The only missing step is UpdateMoveComponent(), which reads the logic position
// and writes it to myTransform (0x740) — but it's skipped when the server hasn't
// sent display data this frame.
//
// Hook ActorLinker::HOK_OnLateUpdate (called every frame per actor):
//   1. Run the original (handles visible actors normally).
//   2. If this actor is in the OOS set, call UpdateMoveComponent() to
//      manually complete the position → Unity Transform sync.
//      Fallback: write directly via Transform::set_position_Injected.
//
// try_lock is used so that if the mutex is held by the network/hook thread
// we skip this frame rather than stalling the main thread — eliminates
// the 2-second HP/skill update delay caused by blocking on the lock.
// =============================================================================

static void (*_HOKLateUpdate)(void* inst, int32_t nDelta) = nullptr;
static void new_HOKLateUpdate(void* inst, int32_t nDelta) {
    // Run the original first (correct for all visible actors)
    if (_HOKLateUpdate) _HOKLateUpdate(inst, nDelta);

    if (!maphack || !inst) return;

    uint32_t objID = *(uint32_t*)((uint64_t)inst + 0x4F4);
    if (!objID) return;

    // Non-blocking lookup — skip this frame if the mutex is contested
    if (!g_oosMtx.try_lock()) return;
    bool isOOS = g_oosSet.count(objID) > 0;
    g_oosMtx.unlock();
    if (!isOOS) return;

    // Game logic position at 0x50C is already current (SGW simulation)
    // — we only need to flush it to the Unity Transform.

    // Primary: let UpdateMoveComponent do its normal sync from MoveComponent
    if (_UpdateMoveComp) {
        _UpdateMoveComp(inst);
        return;
    }

    // Fallback: directly push ActorLinker.position into the Unity Transform
    float* lpos = (float*)((uint64_t)inst + 0x50C);
    void*  myTransform = *(void**)((uint64_t)inst + 0x740);
    if (myTransform && _TransformSetPosInj) {
        float v3[3] = { lpos[0], lpos[1], lpos[2] };
        _TransformSetPosInj(myTransform, v3);
    }
}
