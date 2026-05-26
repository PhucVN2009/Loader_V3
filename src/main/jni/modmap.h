#pragma once
#include <cstdint>

// Map Hack – disables Fog of War so all enemies are visible on map and minimap
// All three hooks target static methods in class FogOfWar (Scripts.GameCore.dll,
// global namespace). All functions are 0-arg, no implicit "this".

bool maphack = false;

// ---------------------------------------------------------------------------
// FogOfWar::IsEnable()
// Primary check used throughout the FOW system to decide if FOW is active.
// Returning false = FOW disabled = all positions visible.
// ---------------------------------------------------------------------------
static bool (*_FowIsEnable)() = nullptr;
static bool new_FowIsEnable() {
    if (maphack) return false;
    if (!_FowIsEnable) return false;
    return _FowIsEnable();
}

// ---------------------------------------------------------------------------
// FogOfWar::get_enable()
// Getter for the private static _enable backing field.
// ---------------------------------------------------------------------------
static bool (*_FowGetEnable)() = nullptr;
static bool new_FowGetEnable() {
    if (maphack) return false;
    if (!_FowGetEnable) return false;
    return _FowGetEnable();
}

// ---------------------------------------------------------------------------
// FogOfWar::get_EnableRender()
// Controls whether the fog texture is composited onto the scene / minimap.
// Returning false removes the visual fog overlay.
// ---------------------------------------------------------------------------
static bool (*_FowGetEnableRender)() = nullptr;
static bool new_FowGetEnableRender() {
    if (maphack) return false;
    if (!_FowGetEnableRender) return false;
    return _FowGetEnableRender();
}
