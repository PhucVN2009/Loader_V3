// --- C Standard ---
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <math.h>
#include <limits>

// --- C++ Standard ---
#include <thread>
#include <vector>
#include <fstream>

// --- Android / System / Dynamic loading ---
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/system_properties.h>

// --- Graphics ---
#include <EGL/egl.h>
#include <GLES3/gl3.h>

// --- External libs / hooks / utilities ---
#include <xdl.h>
#include <SubstrateHook.h>
#include <CydiaSubstrate.h>

// --- KittyMemory (memory tools) ---
#include <KittyMemory/KittyMemory.h>
#include <KittyMemory/MemoryPatch.h>
#include <KittyMemory/KittyScanner.h>
#include <KittyMemory/KittyUtils.h>
#include <KittyUtils.h>

// --- ImGui ---
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

// --- Project / Local headers ---
#include "include/Tools.hpp"
#include "include/obfuscate.h"
#include "include/Theme.h"
#include "imgui/Font.h"
#include "imgui/Roboto-Regular.h"
#include "QQInj.h"
#include "modskin.h"
#include "imgui/Icon.h"
#include "imgui/Iconcpp.h"
#include "AutoUpdate/IL2CppSDKGenerator/Il2Cpp.h"
#include "AutoUpdate/Tools/Call_Tools.h"
#include "Zygisk.hpp"


inline static int g_GlHeight, g_GlWidth;
inline static bool g_IsSetup = false;
inline int prevWidth, prevHeight;

std::string GetProp(const char* key) {
  char value[PROP_VALUE_MAX];
  __system_property_get(key, value);
  return std::string(value);
}

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// Your Game Package Name.
char packageName[] = "com.levelinfinite.sgameGlobal.midaspay";


// You can write your hook here.
//public int get_playerSkin() { }

bool SkinHack = false;
int skinID = 49;

int (*org_skin)(void* instance);
int new_skin(void*instance) {
    if (SkinHack) {
       return skinID;
    }
    return org_skin(instance);
}

// public bool get_Visible() { }
// public COM_PLAYERCAMP get_objCamp() { }
enum COM_PLAYERCAMP {
    ComPlayercampMid = 0,
    ComPlayercamp1   = 1,
    ComPlayercamp2   = 2,
};

bool ShowVisible = false;

int (*org_get_objCamp)(void* instance);

static inline bool isEnemyCamp(void* instance) {
    if (!instance || !org_get_objCamp) return false;
    COM_PLAYERCAMP camp = (COM_PLAYERCAMP)org_get_objCamp(instance);
    return camp == ComPlayercamp1 || camp == ComPlayercamp2;
}

// Hook get_Visible: force render visible for enemies
bool (*org_get_Visible)(void* instance);
bool new_get_Visible(void* instance) {
    if (ShowVisible && isEnemyCamp(instance)) return true;
    if (!org_get_Visible) return false;
    return org_get_Visible(instance);
}

// Hook SetVisible(bLogicVisible, bMeshVisible):
// Prevents game from freezing enemy position when they leave sight.
// bLogicVisible=false stops position updates — we force it true for enemies.
void (*_ActorSetVisible)(void* instance, bool bLogicVisible, bool bMeshVisible);
void new_ActorSetVisible(void* instance, bool bLogicVisible, bool bMeshVisible) {
    if (ShowVisible && isEnemyCamp(instance)) {
        bLogicVisible = true;
        bMeshVisible  = true;
    }
    if (_ActorSetVisible) _ActorSetVisible(instance, bLogicVisible, bMeshVisible);
}





void hack();
void writeLog(const std::string& logMessage, const std::string& filename = "/storage/emulated/0/Android/data/com.waxmoon.ma.gp/files/log.txt");

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api_ = api;
        this->env_ = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *process = env_->GetStringUTFChars(args->nice_name, nullptr);
		
        is_game_ = (strcmp(process, packageName) == 0);

        env_->ReleaseStringUTFChars(args->nice_name, process);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (is_game_) {
            std::thread{hack}.detach();
        }
    }

private:
    Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool is_game_ = false;
};


uintptr_t il2cpp_base = 0;
void *getRealAddr(ulong offset) {
  return reinterpret_cast<void*>(il2cpp_base + offset);
};


void SetupImgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplAndroid_Init(nullptr);
  ImGuiIO& io = ImGui::GetIO();
  
  SetYetAnotherDarkTheme(); //Theme
  
  ImGuiStyle *style = &ImGui::GetStyle();
  ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5f, 0.5f);
  ImGui::GetStyle().FrameBorderSize = 1.5f;
  ImGui::GetStyle().ScrollbarSize = 50.0f;
  ImGui::GetStyle().GrabMinSize = 20.0f;
  ImGui::GetStyle().FrameRounding = 8;
  ImGui::GetStyle().ScrollbarRounding = 12;

  ImGui::GetStyle().PopupRounding = 3;
  ImGui::GetStyle().WindowPadding = ImVec2(4, 4);
  ImGui::GetStyle().FramePadding = ImVec2(2, 2);
  ImGui::GetStyle().ItemSpacing = ImVec2(3, 3);

  ImGui::GetStyle().WindowBorderSize = 4;
  ImGui::GetStyle().ChildBorderSize = 1;
  ImGui::GetStyle().PopupBorderSize = 1;

  ImGui::GetStyle().WindowRounding = 3;
  ImGui::GetStyle().ChildRounding = 3;
  ImGui::GetStyle().GrabRounding = 3;

  ImGui_ImplOpenGL3_Init("#version 100");
  io.IniFilename = nullptr;

  // ---------------- Fonts ----------------
  // 1. Main font: Roboto (supports Cyrillic)
  ImFontConfig font_cfg;
  font_cfg.SizePixels = 40.0f;
  static const ImWchar ranges[] = {
    0x0020,
    0x00FF,
    // Basic Latin + Latin Supplement
    0x0100,
    0x024F,
    // Extended Latin
    0x0400,
    0x052F,
    // Cyrillic
    0
  };
  ImFont* mainFont = io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 40.0f, &font_cfg, ranges);

  // 2. Font Awesome icons (merged into main font, separate size)
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.OversampleH = 2;
  icons_config.OversampleV = 2;
  static const ImWchar icons_ranges[] = {
    ICON_MIN_FA,
    ICON_MAX_FA,
    0
  };
  io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data, font_awesome_size, 40.0f, &icons_config, icons_ranges);

  // 3. Optional Custom font (merged)
  if (Custom != nullptr) {
    ImFontConfig custom_cfg;
    custom_cfg.MergeMode = true;
    custom_cfg.PixelSnapH = true;
    io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(Custom), sizeof(Custom), 40.0f, &custom_cfg, nullptr);
  }
}


bool clearMousePos = true;
bool ImGuiOK = false;


struct UnityEngine_Vector2_Fields {
  float x;
  float y;
};

struct UnityEngine_Vector2_o {
  UnityEngine_Vector2_Fields fields;
};

enum TouchPhase {
  Began = 0,
  Moved = 1,
  Stationary = 2,
  Ended = 3,
  Canceled = 4
};

struct UnityEngine_Touch_Fields {
  int32_t m_FingerId;
  struct UnityEngine_Vector2_o m_Position;
  struct UnityEngine_Vector2_o m_RawPosition;
  struct UnityEngine_Vector2_o m_PositionDelta;
  float m_TimeDelta;
  int32_t m_TapCount;
  int32_t m_Phase;
  int32_t m_Type;
  float m_Pressure;
  float m_maximumPossiblePressure;
  float m_Radius;
  float m_RadiusVariance;
  float m_AltitudeAngle;
  float m_AzimuthAngle;
};



struct Point {
  ImVec2 position;
  ImVec2 velocity;
  float radius;
};

static std::vector < Point > points;

float randomFloat(float min, float max) {
  return min + (rand() / (float)RAND_MAX) * (max - min);
}

void BackGroundDots(int numberOfDots) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 windowSize = ImGui::GetIO().DisplaySize;

  // Время для движения и цвета
  static auto lastTime = std::chrono::high_resolution_clock::now();
  auto currentTime = std::chrono::high_resolution_clock::now();
  std::chrono::duration < float > deltaTime = currentTime - lastTime;
  lastTime = currentTime;
  float t = std::chrono::duration < float > (currentTime.time_since_epoch()).count();

  // Удаляем точки вне экрана
  points.erase(std::remove_if(points.begin(), points.end(), [&](const Point& p) {
    return (p.position.x < 0 - p.radius || p.position.x > windowSize.x + p.radius ||
      p.position.y < 0 - p.radius || p.position.y > windowSize.y + p.radius);
  }), points.end());

  // Добавляем новые точки
  while (points.size() < numberOfDots) {
    Point newPoint;
    newPoint.position.x = randomFloat(0, windowSize.x);
    newPoint.position.y = randomFloat(0, windowSize.y);
    newPoint.velocity.x = randomFloat(-30.0f, 30.0f);
    newPoint.velocity.y = randomFloat(-30.0f, 30.0f);
    newPoint.radius = randomFloat(2.0f, 4.0f);
    points.push_back(newPoint);
  }

  // Обновление и отрисовка точек
  for (int i = 0; i < points.size(); ++i) {
    // Движение точки
    points[i].position.x += points[i].velocity.x * deltaTime.count();
    points[i].position.y += points[i].velocity.y * deltaTime.count();

    // Отскок от границ
    if (points[i].position.x < 0) {
      points[i].position.x = 0; points[i].velocity.x *= -1;
    }
    if (points[i].position.x > windowSize.x) {
      points[i].position.x = windowSize.x; points[i].velocity.x *= -1;
    }
    if (points[i].position.y < 0) {
      points[i].position.y = 0; points[i].velocity.y *= -1;
    }
    if (points[i].position.y > windowSize.y) {
      points[i].position.y = windowSize.y; points[i].velocity.y *= -1;
    }

    // Динамический цвет точки (циклический)
    float r = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t + i));
    float g = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t + i + 2.0f));
    float b = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t + i + 4.0f));
    ImVec4 dotColor = ImVec4(r, g, b, 0.8f);

    // Соединение с другими точками
    float maxDist = 60.0f;
    for (int j = i + 1; j < points.size(); ++j) {
      float dx = points[i].position.x - points[j].position.x;
      float dy = points[i].position.y - points[j].position.y;
      float dist2 = dx*dx + dy*dy;
      if (dist2 < maxDist*maxDist) {
        float alpha = 2.0f - (sqrtf(dist2) / maxDist);
        ImVec4 lineColor = ImVec4(0.2f, 0.2f, 0.3f, alpha * 0.7f);
        draw_list->AddLine(points[i].position, points[j].position, ImGui::ColorConvertFloat4ToU32(lineColor), 1.0f);
      }
    }

    // Отрисовка самой точки
    draw_list->AddCircleFilled(points[i].position, points[i].radius, ImGui::ColorConvertFloat4ToU32(dotColor));
  }
}


ImVec4 HSVtoRGB(float h, float s, float v) {
  float r,
  g,
  b;

  int i = int(h * 6.0f);
  float f = h * 6.0f - i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - f * s);
  float t = v * (1.0f - (1.0f - f) * s);

  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }

  return ImVec4(r, g, b, 1.0f);
}

#include <ctime>
#include <cstdio>

time_t GetExpiryTimestamp(const char* expiry_date_str) {
    // expiry_date_str format is "DD-MM-YY"
    struct tm expiry_tm = {0};
    int day, month, year;
    if (sscanf(expiry_date_str, "%d-%d-%d", &day, &month, &year) != 3) {
        return 0; // invalid format fallback, never expires
    }
    expiry_tm.tm_mday = day;
    expiry_tm.tm_mon = month - 1; // tm_mon is 0-11
    expiry_tm.tm_year = (year < 100) ? (year + 100) : year; // 2000-based year (e.g., 25 -> 2025)
    expiry_tm.tm_hour = 0;
    expiry_tm.tm_min = 0;
    expiry_tm.tm_sec = 0;
    expiry_tm.tm_isdst = -1; // let system determine
    return mktime(&expiry_tm);
}

// Global toggle
static bool g_ShowMenu = false;

void DrawLogo() {
  if (!ImGuiOK) return;
  float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
  ImVec4 rainbow = HSVtoRGB(hue, 1.0f, 1.0f);
  
  static time_t expiry_timestamp = GetExpiryTimestamp("28-10-35"); // Add your Expiry date here. (Date/Month/year)
  time_t now = time(nullptr);
  ImVec2 window_size = ImGui::GetIO().DisplaySize;

  // If expired, show expiry message centered and skip rest of menu
  if (now > expiry_timestamp && expiry_timestamp != 0) {
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::SetNextWindowPos(ImVec2(window_size.x / 2, window_size.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("EXPIRED", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
	ImGui::PushStyleColor(ImGuiCol_Text, rainbow);
    ImGui::Text("- Note : ModMenu is expired -");
    ImGui::End();
	ImGui::PopStyleColor(1);
    return; // Prevent drawing the rest of the menu when expired
  }

  ImGui::SetNextWindowPos(ImVec2(200, 200), ImGuiCond_FirstUseEver);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
  ImGuiWindowFlags_AlwaysAutoResize |
  ImGuiWindowFlags_NoTitleBar |
  ImGuiWindowFlags_NoBackground;
  
  ImGui::Begin("Logo", nullptr, flags);

  float size = 100.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, size * 0.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));

  // === Glass effect background (fake blur with alpha rect) ===
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 rect = ImVec2(pos.x + size, pos.y + size);
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(pos, rect, IM_COL32(255, 255, 255, 60), size * 0.5f); // frosted background

  // === Transparent button colors ===
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.35f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));

  // === Animated text color (rainbow effect) ===
  hue += ImGui::GetIO().DeltaTime * 0.3f; // Speed of color change
  if (hue > 1.0f) hue -= 1.0f;

  ImVec4 textColor = ImColor::HSV(hue, 0.8f, 1.0f); // HSV to RGB conversion
  ImGui::PushStyleColor(ImGuiCol_Text, textColor);

  ImGui::Button(ICON_FA_POWER_OFF, ImVec2(size, size));

  static bool dragging = false;

  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    dragging = true;
    ImVec2 delta = ImGui::GetIO().MouseDelta;
    ImVec2 wpos = ImGui::GetWindowPos();
    ImGui::SetWindowPos(ImVec2(wpos.x + delta.x, wpos.y + delta.y));
  }

  if (ImGui::IsItemDeactivated()) {
    if (!dragging && ImGui::IsItemHovered()) {
      g_ShowMenu = !g_ShowMenu;
    }
    dragging = false;
  }

  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  ImGui::End();
}

inline ImVec2 operator*(const ImVec2& v, float s) {
  return ImVec2(v.x * s, v.y * s);
}


void DrawMenu() {
    static int activeFeature = 0;

    if (!g_ShowMenu) return;

    const ImVec2 window_size = ImVec2(600, 600);
    ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
    ImVec2 pos = ImVec2(center.x - window_size.x * 0.5f, center.y - window_size.y * 0.5f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Once);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("IMGUI MODMENU", nullptr, flags);

    float screenHeight = ImGui::GetIO().DisplaySize.y;
    if (screenHeight > 720)
        BackGroundDots(250);
    else
        BackGroundDots(125);

    ImGui::SetCursorPos(ImVec2(20, 15));
    float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
    ImVec4 rainbow = HSVtoRGB(hue, 1.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Separator, rainbow);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, rainbow);
    ImGui::TextColored(rainbow, ICON_FA_SUN " VIP MODMENU BY - YOUR NAME -");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float padding = 15.0f;
    float totalPadding = padding * 3;
    float buttonWidth = (window_size.x - totalPadding) / 2.0f;
    float buttonHeight = 65.0f;

    ImGui::SetCursorPosX(padding);
    ImGui::PushID(1);
    if (ImGui::Button(ICON_FA_HOME, ImVec2(buttonWidth, buttonHeight))) {
        activeFeature = 0;
    }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::SetCursorPosX(padding * 2 + buttonWidth);
    ImGui::PushID(2);
    if (ImGui::Button(ICON_FA_DATABASE, ImVec2(buttonWidth, buttonHeight))) {
        activeFeature = 1;
    }
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (activeFeature == 0) {
        ImGui::Checkbox("Set Skin", &SkinHack);
        ImGui::SliderInt("ID", &skinID, 1, 49);
        ImGui::Separator();
        ImGui::Checkbox("Show Enemy (Antifog)", &ShowVisible);
        ImGui::Separator();

        // ── Unlock Skin ────────────────────────────────────────────────
        if (ImGui::Checkbox("Unlock Skin", &unlockskin)) {
            if (!unlockskin) CSProtocol::saveData::resetArrayUnpackSkin();
        }
        if (unlockskin) {
            ImGui::SameLine();
            // Mode toggle buttons
            ImGui::PushID("skinmode");
            if (ImGui::Button(skinMode == 0 ? "[Auto]" : " Auto ", ImVec2(80, 0)))
                skinMode = 0;
            ImGui::SameLine();
            if (ImGui::Button(skinMode == 1 ? "[Custom]" : " Custom ", ImVec2(90, 0)))
                skinMode = 1;
            ImGui::PopID();

            if (skinMode == 0) {
                ImGui::TextColored(ImColor(0,255,180), "Bat len roi chon skin trong man chon tuong la tu dong ap dung.");
            } else {
                ImGui::InputInt("Hero ID", &heroid);
                ImGui::InputInt("Skin ID", &skinid);
                if (ImGui::Button("Apply##skin")) {
                    CSProtocol::saveData::setData((uint32_t)heroid, (uint16_t)skinid);
                    CSProtocol::saveData::setEnable(true);
                }
            }
        }

        ImGui::Separator();

        // ── Unlock Button ──────────────────────────────────────────────
        if (ImGui::Checkbox("Unlock Button", &unlockbutton)) {
            if (!unlockbutton) CSProtocol::saveData::resetArrayUnpackSkin();
        }
        if (unlockbutton) {
            ImGui::InputInt("Hero ID 2", &heroid2);
            ImGui::InputInt("Skin ID 2", &skinid2);
            if (ImGui::Button("Apply##btn")) {
                CSProtocol::saveData::setData((uint32_t)heroid2, (uint16_t)skinid2);
                CSProtocol::saveData::setEnable(true);
            }
        }
    }
    else if (activeFeature == 1) {
        ImGui::Columns(2, "deviceInfo", false);

        ImGui::TextColored(ImColor(255, 200, 0), "Device Name:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.device").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "Model:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.model").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "Manufacturer:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.manufacturer").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "Android Version:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.build.version.release").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "SDK Version:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.build.version.sdk").c_str());
        ImGui::NextColumn();

        ImGui::TextColored(ImColor(255, 200, 0), "CPU ABI:");
        ImGui::NextColumn();
        ImGui::TextColored(ImColor(0, 255, 255), "%s", GetProp("ro.product.cpu.abi").c_str());
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}



inline EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
inline EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {

  eglQuerySurface(dpy, surface, EGL_WIDTH, &g_GlWidth);
  eglQuerySurface(dpy, surface, EGL_HEIGHT, &g_GlHeight);
  
  static bool should_clear_mouse_pos = false;

  if (!g_IsSetup) {
    prevWidth = g_GlWidth;
    prevHeight = g_GlHeight;
    SetupImgui();

    g_IsSetup = true;
  }
  ImGuiIO &io = ImGui::GetIO();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame(g_GlWidth, g_GlHeight);
  ImGui::NewFrame();
  if (ImGuiOK) {
    int (*TouchCount)(void*) = (int (*)(void*)) Il2CppGetMethodOffset("UnityEngine.dll", "UnityEngine", "Input", "get_touchCount", 0);
    int touchCount = TouchCount(nullptr);
    if (touchCount > 0) {
      UnityEngine_Touch_Fields touch = ((UnityEngine_Touch_Fields (*)(int)) Il2CppGetMethodOffset("UnityEngine.dll", "UnityEngine", "Input", "GetTouch", 1)) (0);
      float reverseY = io.DisplaySize.y - touch.m_Position.fields.y;

      switch (touch.m_Phase) {
        case TouchPhase::Began:
        case TouchPhase::Stationary:
        io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
        io.MouseDown[0] = true;
        break;
        case TouchPhase::Ended:
        case TouchPhase::Canceled:
        io.MouseDown[0] = false;
        should_clear_mouse_pos = true;
        break;
        case TouchPhase::Moved:
        io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
        break;
        default:
        break;
      }
    } else {
      io.MouseDown[0] = false;
    }
  }
  
  DrawLogo();
  DrawMenu();
  
  ImGui::End();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  ImGui::EndFrame();
  if (should_clear_mouse_pos) {
    io.MousePos = ImVec2(-1, -1);
    should_clear_mouse_pos = false;
  }

  return old_eglSwapBuffers(dpy, surface);
}







typedef unsigned long DWORD;
static uintptr_t libBase;

uintptr_t string2Offset(const char *c) {
  int base = 16;
  // See if this function catches all possibilities.
  // If it doesn't, the function would have to be amended
  // whenever you add a combination of architecture and
  // compiler that is not yet addressed.
  static_assert(sizeof(uintptr_t) == sizeof(unsigned long) || sizeof(uintptr_t) == sizeof(unsigned long long),
    "Please add string to handle conversion for this architecture.");

  // Now choose the correct function ...
  if (sizeof(uintptr_t) == sizeof(unsigned long)) {
    return strtoul(c, nullptr, base);
  }

  // All other options exhausted, sizeof(uintptr_t) == sizeof(unsigned long long))
  return strtoull(c, nullptr, base);
}


inline void hack_injec();
inline void StartGUI() {
  void *ptr_eglSwapBuffer = DobbySymbolResolver("/system/lib/libEGL.so", "eglSwapBuffers");
  if (NULL != ptr_eglSwapBuffer) {
    DobbyHook((void *)ptr_eglSwapBuffer, (void*)hook_eglSwapBuffers, (void**)&old_eglSwapBuffers);
    LOGD("Gui Started");
    hack_injec();
  }
}

bool libLoaded = false;

DWORD findLibrary(const char *library) {
  char filename[0xFF] = {
    0
  },
  buffer[1024] = {
    0
  };
  FILE *fp = NULL;
  DWORD address = 0;

  sprintf(filename, OBFUSCATE("/proc/self/maps"));

  fp = fopen(filename, OBFUSCATE("rt"));
  if (fp == NULL) {
    perror(OBFUSCATE("fopen"));
    goto done;
  }

  while (fgets(buffer, sizeof(buffer), fp)) {
    if (strstr(buffer, library)) {
      address = (DWORD) strtoul(buffer, NULL, 16);
      goto done;
    }
  }

  done:

  if (fp) {
    fclose(fp);
  }

  return address;
}

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr) {
  libBase = findLibrary(libraryName);
  if (libBase == 0)
  return 0;
  return (reinterpret_cast<DWORD > (libBase + relativeAddr));
}
ProcMap unityMap, anogsMap, il2cppMap;
using KittyScanner::RegisterNativeFn;

void hack() {
  LOGD("Inject Ok");
}
uintptr_t get_symbol_addr_in_pid(pid_t pid, const char* libname, uintptr_t offset_in_lib) {
  char maps_path[64];
  snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

  FILE* fp = fopen(maps_path, "r");
  if (!fp) return 0;

  char line[512];
  uintptr_t base = 0;

  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, libname)) {
      sscanf(line, "%lx-%*lx", &base);
      break;
    }
  }
  fclose(fp);

  if (base == 0) return 0;
  return base + offset_in_lib;
}

pid_t get_pid_by_name(const char* process_name) {
  DIR* proc_dir = opendir("/proc");
  if (!proc_dir) return -1;

  struct dirent* entry;
  while ((entry = readdir(proc_dir)) != NULL) {
    if (entry->d_type != DT_DIR) continue;

    pid_t pid = atoi(entry->d_name);
    if (pid <= 0) continue;

    char cmdline_path[256];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);

    FILE* fp = fopen(cmdline_path, "r");
    if (!fp) continue;

    char cmdline[256];
    fgets(cmdline, sizeof(cmdline), fp);
    fclose(fp);

    if (strstr(cmdline, process_name)) {
      closedir(proc_dir);
      return pid;
    }
  }

  closedir(proc_dir);
  return -1;
}

void writeLog(const std::string& logMessage, const std::string& filename) {
  std::ofstream outFile(filename, std::ios::app);
  if (outFile.is_open()) {
    outFile << logMessage << std::endl;
    outFile.close();
  } else {
    //std::cerr << "Log file log: << filename << std::endl;
  }
}

bool is_current_process(const char* target_name) {
  char cmdline_path[64];
  snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", getpid());

  FILE* fp = fopen(cmdline_path, "r");
  if (!fp) return false;

  char cmdline[256] = {
    0
  };
  fgets(cmdline, sizeof(cmdline), fp);
  fclose(fp);

  return strcmp(cmdline, target_name) == 0;
}



void hack_injec() {
  while (!unityMap.isValid()) {
    unityMap = KittyMemory::getLibraryBaseMap("libunity.so");
    il2cppMap = KittyMemory::getLibraryBaseMap("libil2cpp.so");
    sleep(5);
  }
  sleep(5);
  Il2CppAttach("libil2cpp.so");
  // Write Your bypass/AutoUpdate Hooks
  DobbyHook(Il2CppGetMethodOffset("Assembly-CSharp.dll", "", "GameParamsScript", "get_playerSkin", 0), (void*)new_skin, (void**)&org_skin);

  // Antifog
  org_get_objCamp = (int (*)(void*)) Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_objCamp", 0);

  void* visAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "get_Visible", 0);
  if (visAddr) DobbyHook(visAddr, (void*)new_get_Visible, (void**)&org_get_Visible);

  // Fix enemy frozen position: intercept SetVisible so bLogicVisible stays true
  void* svAddr = Il2CppGetMethodOffset("Scripts.GameCore.dll", "Assets.Scripts.GameLogic", "ActorLinker", "SetVisible", 2);
  if (svAddr) DobbyHook(svAddr, (void*)new_ActorSetVisible, (void**)&_ActorSetVisible);

  // Unlock Skin hooks
  void* skAddr;
  skAddr = Il2CppGetMethodOffset("Scripts.Plugins.dll", "CSProtocol", "COMDT_HERO_COMMON_INFO", "unpack", 2);
  if (skAddr) DobbyHook(skAddr, (void*)new_unpack, (void**)&_unpack);

  skAddr = Il2CppGetMethodOffset("Scripts.Base.dll", "Assets.Scripts.GameSystem", "CRoleInfo", "IsCanUseSkin", 3);
  if (skAddr) DobbyHook(skAddr, (void*)new_IsCanUseSkin, (void**)&_IsCanUseSkin);

  skAddr = Il2CppGetMethodOffset("Scripts.Base.dll", "Assets.Scripts.GameSystem", "CRoleInfo", "IsHaveHeroSkin", 4);
  if (skAddr) DobbyHook(skAddr, (void*)new_IsHaveHeroSkin, (void**)&_IsHaveHeroSkin);

  skAddr = Il2CppGetMethodOffset("Scripts.System.dll", "Assets.Scripts.GameSystem", "CSelectHeroFormLogic", "GetHeroWearSkinId", 1);
  if (skAddr) DobbyHook(skAddr, (void*)new_GetHeroWearSkinId, (void**)&_GetHeroWearSkinId);

  skAddr = Il2CppGetMethodOffset("Scripts.System.dll", "Assets.Scripts.GameSystem", "CSelectHeroFormLogic", "WearHeroSkin", 2);
  if (skAddr) DobbyHook(skAddr, (void*)new_WearHeroSkin, (void**)&_WearHeroSkin);

  // DobbyHook(Il2CppGetMethodOffset("Assembly-CSharp.dll", "Namespace", "class", "method", 0), (void*)new_hook, (void**)&org_func);
  ImGuiOK = true;
}


void hack_thread(pid_t pid) {

  StartGUI();
  while(pid == -1) {
    pid = get_pid_by_name(packageName);
  }
  remote_inject(pid);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void * reserved) {
  JNIEnv *env;
  vm->GetEnv((void **) &env, JNI_VERSION_1_6);
  return JNI_VERSION_1_6;
}

__attribute__((constructor))
void lib_main() {
  std::thread thread_hack(hack_thread, get_pid_by_name(packageName));
  thread_hack.detach();
}
