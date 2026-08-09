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
  
  SetYetAnotherDarkTheme(); // Base theme; refined below for the touch-first menu.
  
  ImGuiStyle *style = &ImGui::GetStyle();
  style->WindowTitleAlign = ImVec2(0.5f, 0.5f);
  style->WindowPadding = ImVec2(16.0f, 16.0f);
  style->FramePadding = ImVec2(14.0f, 10.0f);
  style->ItemSpacing = ImVec2(10.0f, 12.0f);
  style->ItemInnerSpacing = ImVec2(10.0f, 8.0f);
  style->FrameBorderSize = 0.0f;
  style->WindowBorderSize = 1.0f;
  style->ChildBorderSize = 1.0f;
  style->PopupBorderSize = 1.0f;
  style->ScrollbarSize = 18.0f;
  style->GrabMinSize = 18.0f;
  style->WindowRounding = 18.0f;
  style->ChildRounding = 14.0f;
  style->FrameRounding = 10.0f;
  style->PopupRounding = 10.0f;
  style->ScrollbarRounding = 10.0f;
  style->GrabRounding = 10.0f;

  ImVec4* colors = style->Colors;
  colors[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 1.00f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.52f, 0.64f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.043f, 0.075f, 0.98f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.075f, 0.12f, 0.94f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.065f, 0.105f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.25f, 0.28f, 0.43f, 0.55f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.11f, 0.18f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.16f, 0.29f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.20f, 0.42f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.45f, 1.00f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.49f, 0.72f, 1.00f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.68f, 0.50f, 1.00f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.11f, 0.12f, 0.20f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.20f, 0.42f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.25f, 0.56f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.19f, 0.17f, 0.32f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.22f, 0.47f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.36f, 0.27f, 0.62f, 1.00f);
  colors[ImGuiCol_Separator] = ImVec4(0.23f, 0.25f, 0.38f, 0.65f);

  ImGui_ImplOpenGL3_Init("#version 100");
  io.IniFilename = nullptr;

  // ---------------- Fonts ----------------
  // 1. Main font: Roboto (supports Cyrillic)
  ImFontConfig font_cfg;
  font_cfg.SizePixels = 32.0f;
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
  ImFont* mainFont = io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 32.0f, &font_cfg, ranges);

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
  io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data, font_awesome_size, 32.0f, &icons_config, icons_ranges);

  // 3. Optional Custom font (merged)
  if (Custom != nullptr) {
    ImFontConfig custom_cfg;
    custom_cfg.MergeMode = true;
    custom_cfg.PixelSnapH = true;
    io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(Custom), sizeof(Custom), 32.0f, &custom_cfg, nullptr);
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

    static time_t expiry_timestamp = GetExpiryTimestamp("28-10-35");
    const time_t now = time(nullptr);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (now > expiry_timestamp && expiry_timestamp != 0) {
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.96f);
        ImGui::Begin("##expired", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.42f, 1.0f), ICON_FA_TIMES_CIRCLE "  Menu expired");
        ImGui::TextDisabled("Please install a current build.");
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(48.0f, display.y * 0.35f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(82.0f, 82.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.055f, 0.10f, 0.96f));
    ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.24f, 0.70f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.47f, 0.34f, 0.88f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.66f, 0.98f, 1.0f));
    ImGui::Button(g_ShowMenu ? ICON_FA_TIMES : ICON_FA_CROWN, ImVec2(72, 72));

    static bool dragging = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 8.0f)) {
        dragging = true;
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(p.x + d.x, p.y + d.y));
    }
    if (ImGui::IsItemDeactivated()) {
        if (!dragging && ImGui::IsItemHovered()) g_ShowMenu = !g_ShowMenu;
        dragging = false;
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

inline ImVec2 operator*(const ImVec2& v, float s) {
    return ImVec2(v.x * s, v.y * s);
}

static bool MenuTab(const char* icon, const char* label, bool selected) {
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 11.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.34f, 0.24f, 0.68f, 1.0f) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.19f, 0.18f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4(1, 1, 1, 1) : ImVec4(0.58f, 0.62f, 0.74f, 1));
    char caption[96];
    snprintf(caption, sizeof(caption), "%s   %s", icon, label);
    const bool pressed = ImGui::Button(caption, ImVec2(-1, 58));
    if (selected) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        draw->AddRectFilled(ImVec2(min.x, min.y + 12), ImVec2(min.x + 4, max.y - 12), IM_COL32(120, 196, 255, 255), 2.0f);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return pressed;
}

static bool ToggleSwitch(const char* id, bool* value) {
    const float height = 34.0f;
    const float width = 64.0f;
    ImGui::InvisibleButton(id, ImVec2(width, height));
    if (ImGui::IsItemClicked()) *value = !*value;
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 bg = *value ? IM_COL32(112, 78, 230, 255) : IM_COL32(55, 59, 78, 255);
    draw->AddRectFilled(min, max, bg, height * 0.5f);
    const float x = *value ? max.x - height * 0.5f : min.x + height * 0.5f;
    draw->AddCircleFilled(ImVec2(x, min.y + height * 0.5f), 13.0f, IM_COL32(245, 247, 255, 255));
    return ImGui::IsItemClicked();
}

static void SectionTitle(const char* icon, const char* title, const char* description) {
    ImGui::TextColored(ImVec4(0.62f, 0.48f, 1.0f, 1.0f), "%s  %s", icon, title);
    ImGui::TextDisabled("%s", description);
    ImGui::Spacing();
}

static void DeviceInfoRow(const char* icon, const char* label, const char* property) {
    ImGui::TextColored(ImVec4(0.48f, 0.68f, 1.0f, 1.0f), "%s", icon);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(245.0f);
    ImGui::Text("%s", GetProp(property).c_str());
    ImGui::Separator();
}

static void StatCard(const char* id, const char* icon, const char* value, const char* label, const ImVec4& accent) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.085f, 0.14f, 0.94f));
    ImGui::BeginChild(id, ImVec2(0, 112), true);
    ImGui::TextColored(accent, "%s", icon);
    ImGui::SameLine();
    ImGui::Text("%s", value);
    ImGui::TextDisabled("%s", label);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawMenu() {
    static int activeTab = 0;
    if (!g_ShowMenu) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 menuSize(ImMin(1000.0f, display.x - 32.0f), ImMin(650.0f, display.y - 32.0f));
    ImGui::SetNextWindowPos(display * 0.5f, ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(menuSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##aov_control_center", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowMax = windowPos + menuSize;
    draw->AddRectFilledMultiColor(windowPos, windowMax, IM_COL32(13, 15, 29, 255), IM_COL32(23, 18, 46, 255),
                                  IM_COL32(10, 20, 35, 255), IM_COL32(10, 13, 26, 255));

    const float sidebarWidth = 245.0f;
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.04f, 0.075f, 0.97f));
    ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, menuSize.y), false);
    ImGui::SetCursorPos(ImVec2(22, 24));
    ImGui::TextColored(ImVec4(0.67f, 0.49f, 1.0f, 1.0f), ICON_FA_CROWN);
    ImGui::SameLine();
    ImGui::Text("AOV  TOOL");
    ImGui::SetCursorPosX(22);
    ImGui::TextDisabled("CONTROL CENTER  /  V3");
    ImGui::SetCursorPos(ImVec2(16, 112));
    ImGui::BeginGroup();
    if (MenuTab(ICON_FA_TACHOMETER_ALT, "Dashboard", activeTab == 0)) activeTab = 0;
    if (MenuTab(ICON_FA_TSHIRT, "Skin changer", activeTab == 1)) activeTab = 1;
    if (MenuTab(ICON_FA_MOBILE_ALT, "Device", activeTab == 2)) activeTab = 2;
    ImGui::EndGroup();

    ImGui::SetCursorPos(ImVec2(20, menuSize.y - 100));
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.30f, 0.88f, 0.63f, 1.0f), ICON_FA_WIFI "  CONNECTED");
    ImGui::TextDisabled(ICON_FA_SHIELD_ALT "  Protected session");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(sidebarWidth, 0));
    ImGui::BeginChild("##main", ImVec2(menuSize.x - sidebarWidth, menuSize.y), false);
    ImGui::SetCursorPos(ImVec2(28, 22));
    ImGui::BeginGroup();
    const char* titles[] = {"Dashboard", "Skin Changer", "Device Information"};
    const char* subtitles[] = {"Overview and quick access", "Manage your in-game appearance", "System and hardware details"};
    ImGui::Text("%s", titles[activeTab]);
    ImGui::TextDisabled("%s", subtitles[activeTab]);
    ImGui::EndGroup();
    ImGui::SameLine(menuSize.x - sidebarWidth - 88.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.16f, 0.24f, 1.0f));
    if (ImGui::Button(ICON_FA_TIMES, ImVec2(54, 54))) g_ShowMenu = false;
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(28, 94));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(28, 118));

    const float contentWidth = menuSize.x - sidebarWidth - 56.0f;
    if (activeTab == 0) {
        SectionTitle(ICON_FA_LAYER_GROUP, "OVERVIEW", "Everything you need in one place.");
        const float cardWidth = (contentWidth - 20.0f) / 3.0f;
        ImGui::PushItemWidth(cardWidth);
        ImGui::BeginChild("##stat1wrap", ImVec2(cardWidth, 112), false); StatCard("##stat1", ICON_FA_TSHIRT, SkinHack ? "ACTIVE" : "OFF", "Skin override", ImVec4(0.65f, 0.48f, 1, 1)); ImGui::EndChild();
        ImGui::SameLine(0, 10);
        ImGui::BeginChild("##stat2wrap", ImVec2(cardWidth, 112), false); StatCard("##stat2", ICON_FA_PAINT_BRUSH, std::to_string(skinID).c_str(), "Selected skin ID", ImVec4(0.35f, 0.72f, 1, 1)); ImGui::EndChild();
        ImGui::SameLine(0, 10);
        ImGui::BeginChild("##stat3wrap", ImVec2(cardWidth, 112), false); StatCard("##stat3", ICON_FA_SIGNAL, "READY", "Service status", ImVec4(0.30f, 0.88f, 0.63f, 1)); ImGui::EndChild();
        ImGui::PopItemWidth();
        ImGui::Spacing();
        ImGui::BeginChild("##quick", ImVec2(contentWidth, 205), true);
        SectionTitle(ICON_FA_BOLT, "QUICK CONTROL", "Turn the skin override on or off instantly.");
        ImGui::Text("Skin override");
        ImGui::TextDisabled("Apply the selected skin identifier");
        ImGui::SameLine(contentWidth - 100.0f);
        ToggleSwitch("##quickToggle", &SkinHack);
        ImGui::EndChild();
    } else if (activeTab == 1) {
        ImGui::BeginChild("##skin_panel", ImVec2(contentWidth, 350), true);
        SectionTitle(ICON_FA_MAGIC, "SKIN OVERRIDE", "Choose a skin and apply it without leaving the menu.");
        ImGui::Text("Enable feature");
        ImGui::TextDisabled("Override the default player skin");
        ImGui::SameLine(contentWidth - 100.0f);
        ToggleSwitch("##skinToggle", &SkinHack);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Skin identifier");
        ImGui::TextDisabled("Drag the slider to select an ID from 1 to 49");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##skin_id", &skinID, 1, 49, "ID  %d");
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::TextColored(SkinHack ? ImVec4(0.30f, 0.88f, 0.63f, 1) : ImVec4(0.58f, 0.62f, 0.74f, 1),
                           SkinHack ? ICON_FA_CHECK_CIRCLE "  Override enabled and ready" : ICON_FA_INFO_CIRCLE "  Enable override to apply your selection");
    } else {
        ImGui::BeginChild("##device_panel", ImVec2(contentWidth, 410), true);
        SectionTitle(ICON_FA_MICROCHIP, "DEVICE PROFILE", "Information reported by the Android system.");
        DeviceInfoRow(ICON_FA_MOBILE_ALT, "Device", "ro.product.device");
        DeviceInfoRow(ICON_FA_MOBILE, "Model", "ro.product.model");
        DeviceInfoRow(ICON_FA_WINDOW, "Manufacturer", "ro.product.manufacturer");
        DeviceInfoRow(ICON_FA_ROCKET, "Android", "ro.build.version.release");
        DeviceInfoRow(ICON_FA_COG, "SDK level", "ro.build.version.sdk");
        DeviceInfoRow(ICON_FA_MICROCHIP, "CPU ABI", "ro.product.cpu.abi");
        ImGui::EndChild();
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar();
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

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
