#pragma once
#include <cstdint>

bool maphack = false;

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
// =============================================================================

static void (*_ActorSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) { logicVis = true; meshVis = true; }
    if (_ActorSetVisible) _ActorSetVisible(inst, logicVis, meshVis);
}

static void (*_ActorForceSetVisible)(void* inst, bool logicVis, bool meshVis) = nullptr;
static void new_ActorForceSetVisible(void* inst, bool logicVis, bool meshVis) {
    if (maphack) { logicVis = true; meshVis = true; }
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
