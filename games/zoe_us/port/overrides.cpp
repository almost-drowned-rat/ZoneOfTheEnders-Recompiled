/*
 * Zone of the Enders's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently, and the
 * way the PC features hook in (a settings menu in the game's own menus,
 * widescreen, the mouse's targets). Register them here with
 * ps2host::Override(address, function, "why"); the original stays callable as
 * F_<ADDRESS>(c). See the engine's docs/PORTING.md and the ps2-port skill.
 *
 * Override as little as possible: the game's own code is the point.
 */

#include "ps2host/host.h"
#include "ps2host/input_bind.h"
#include "ps2host/iop_vm.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

#include "pipeline_keys.inc"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

void DubSampleScene();              // dub.cpp: SUBTITLE TIMING's scene, sampled each frame (FrameEnd)
std::string DubJapaneseLine(const std::string& text);   // dub.cpp: the Japanese line a "#J" subtitle stands for
void DescriptionsInSpanish();       // dub.cpp: SPANISH's descriptions in the executable's tables
bool DubSubtitlePack();             // dub.cpp: the subtitle pack (ZOE-subtitles.pak) is there
const std::vector<unsigned char>& DubSubtitleFont();    // ...its Noto Sans JP
void F_00450C40(lifted::Context& c);
void F_00450F58(lifted::Context& c);
void F_004597F0(lifted::Context& c);
void F_00459908(lifted::Context& c);
void F_0044C7D0(lifted::Context& c);
void F_00414B68(lifted::Context& c);
void F_003E6188(lifted::Context& c);
void F_00282A20(lifted::Context& c);
void F_00420E88(lifted::Context& c);
void F_00420FA0(lifted::Context& c);
void F_00422FF0(lifted::Context& c);
void F_00423240(lifted::Context& c);
void F_004235A0(lifted::Context& c);
void F_00461ED0(lifted::Context& c);
void F_0045ABE0(lifted::Context& c);
void F_0045B668(lifted::Context& c);
void F_004663B0(lifted::Context& c);
void F_00466400(lifted::Context& c);
void F_00466450(lifted::Context& c);
void F_004664A0(lifted::Context& c);
void F_00466E38(lifted::Context& c);
void F_00466F18(lifted::Context& c);
void F_00466FB8(lifted::Context& c);

namespace {

// ---- ASPECT RATIO / FIELD OF VIEW (M7 step 5; porting guide 4, 4b) ----
// Game facts (addresses, offsets, what the functions do - no code) from ZOE-Recompiled b6a11d0
// src/zoe_overrides.cpp (GPL-3.0, as config/names.csv's names from its toml): a camera keeps a projection scale
// pair, +0x224 horizontal (1.0) and +0x228 vertical (0.58333 for the game's own camera), and its focal constant at
// +0x244 (500). 0x00450C40(camera) builds the camera's matrices +0x80 / +0xC0 from them (x scale +0x224 / 512, y
// +0x228 / 224) and keeps them; 0x00450F58(camera) composes them with the view each frame (its body skipped when
// the stamp +0x26C equals the game's frame counter 0x004AA288). A wider picture is the same 512 columns holding
// more of the world - the horizontal scale times (4/3) / aspect - which the window then shows at that aspect
// (the engine's anamorphic render: wide pictures get their columns back). The fields are only changed for the
// build and put back after (the game never sees them changed).
constexpr u32 kCamHScale = 0x224u, kCamVScale = 0x228u, kCamFocal = 0x244u, kCamStamp = 0x26Cu, kCamMatrix = 0x80u;
constexpr u32 kFrameCounter = 0x004AA288u;
constexpr u32 kBitsOne = 0x3F800000u, kBitsStockV = 0x3F155555u;     // the game's own camera: 1.0, 0.58333

float F32(u32 bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
u32 Bits(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
u32 Read32(u32 address) { return u32(ps2host::GuestMemory().Read(address, 4)); }
void Write32(u32 address, u32 value) { ps2host::GuestMemory().Write(address, value, 4); }
bool CameraOk(u32 camera) { return camera >= 0x00100000u && camera < 0x02000000u - 0x300u; }

// The factors a camera's pair is built with: horizontal (4/3) / aspect for every camera (a cutscene's or a
// map window's as the game's own: the picture is wider for all of them); FIELD OF VIEW a zoom out of both, for the
// game's own camera (the stock pair) alone - +N degrees on its 4:3 horizontal view, 2 atan(256 / 500) = 54.2.
void Factors(u32 camera, float* h, float* v) {
    *h = (4.0f / 3.0f) / ps2host::WideAspect();
    *v = 1.0f;
    const int extra = ps2host::FieldOfViewExtra();
    if (extra > 0 && Read32(camera + kCamHScale) == kBitsOne && Read32(camera + kCamVScale) == kBitsStockV) {
        const float half = std::atan(256.0f / 500.0f);
        const float zoom = std::tan(half) / std::tan(half + float(extra) * 3.14159265f / 360.0f);
        *h *= zoom;
        *v = zoom;
    }
}

// 0x00450C40 with the camera's pair scaled for the build, put back after (if nothing else changed them since).
void BuildScaled(lifted::Context& c, u32 camera, float h, float v) {
    const u32 hold = Read32(camera + kCamHScale), vold = Read32(camera + kCamVScale);
    const u32 hnew = Bits(F32(hold) * h), vnew = Bits(F32(vold) * v);
    Write32(camera + kCamHScale, hnew);
    Write32(camera + kCamVScale, vnew);
    F_00450C40(c);
    if (Read32(camera + kCamHScale) == hnew) Write32(camera + kCamHScale, hold);
    if (Read32(camera + kCamVScale) == vnew) Write32(camera + kCamVScale, vold);
}

void CameraMatrix(lifted::Context& c) {
    const u32 camera = u32(c.r[4]);
    float h = 1.0f, v = 1.0f;
    if (CameraOk(camera)) Factors(camera, &h, &v);
    if (h == 1.0f && v == 1.0f) {
        F_00450C40(c);
        return;
    }
    BuildScaled(c, camera, h, v);
}

// Each frame a camera is used: its kept matrices built again where they are not this setting's (the setting
// changed, or the game set the camera up before) - told by +0x80's first element, -(+0x224) (+0x244) as built.
void CameraView(lifted::Context& c) {
    const u32 camera = u32(c.r[4]);
    if (CameraOk(camera) && Read32(camera + kCamStamp) != Read32(kFrameCounter)) {
        float h = 1.0f, v = 1.0f;
        Factors(camera, &h, &v);
        const float expected = -F32(Read32(camera + kCamHScale)) * h * F32(Read32(camera + kCamFocal));
        const float built = F32(Read32(camera + kCamMatrix));
        if (expected != 0.0f && std::fabs(built / expected - 1.0f) > 1e-4f) {
            // 0x00450C40(camera) first; the arguments and return address of 0x00450F58's own call kept.
            u64 saved[5], saved_hi[5];
            const int regs[5] = {4, 5, 6, 7, 31};
            for (int i = 0; i < 5; ++i) saved[i] = c.r[regs[i]], saved_hi[i] = c.rh[regs[i]];
            c.r[4] = camera;
            BuildScaled(c, camera, h, v);
            for (int i = 0; i < 5; ++i) c.r[regs[i]] = saved[i], c.rh[regs[i]] = saved_hi[i];
        }
    }
    F_00450F58(c);
}

// 3D drawn (0x004597F0, a 3D object's draw): the picture is the wide one while it was in the last few blanks;
// screens without it (title, menus on black, GAME OVER, movies) stand 4:3 between bars.
// A gap in the 3D alone is no 4:3 screen (2026-11-20 issue 5: the window jumped 16:9 <-> 4:3 at every load, hitch
// or shader wait while the cameras stayed wide): 4:3 only once the game went on stepping without 3D (its frame
// counter 10 steps past the last 3D draw) - or, should a screen not step that counter, after 2 s without 3D.
std::atomic<u64> g_last_3d_ns{0};
std::atomic<u32> g_last_3d_step{0};
void Object3d(lifted::Context& c) {
    g_last_3d_ns.store(ps2host::LastVblankNs(), std::memory_order_relaxed);
    g_last_3d_step.store(Read32(kFrameCounter), std::memory_order_relaxed);
    F_004597F0(c);
}
// The maintainer's choice (2026-11-20, variant B): the chosen shape ALWAYS - play, in-engine scenes, menus, loads -
// except while a movie runs (its pictures are 4:3, made so): 4:3 between bars from its first picture to ten blanks
// after its last (the native decoder's, LastMoviePictureNs). The 3D sign above stays for the HUD's own placing.
bool ShowsWide() {
    if (ps2host::SettingOn("stretch_cutscenes", false)) return true;      // STRETCH CUTSCENES ON: never 4:3
    const u64 movie = ps2host::LastMoviePictureNs();
    return !movie || ps2host::LastVblankNs() - movie > 10u * 16683334u;
}

// 3D on the screen (a 3D draw within the game's last 10 steps, or 0.5 s): the HUD's pieces then keep to their edge
// (HudSprite / HudText / windows); a screen of 2D alone (the title, menus on black) is placed about the centre as
// one - its logo's pieces no longer pulled to the left edge in a wide shape (his variant B, 2026-11-20).
bool Shows3d() {
    const u64 last = g_last_3d_ns.load(std::memory_order_relaxed);
    if (!last) return false;
    if (ps2host::LastVblankNs() - last >= 30u * 16683334u) return false;
    return Read32(kFrameCounter) - g_last_3d_step.load(std::memory_order_relaxed) < 10u;
}

// ---- HUD (M7 step 5, part 2): 0x00459908(object), a HUD sprite's draw - its packet built from the object
// (0x00459230): flags +0x02 (u16; 0x02 hidden, 0x20 GS coordinates, 0x8000 origin-centred), axis rows +0x20 /
// +0x30, position +0x50 / +0x54 (0..512 x 0..448), depth +0x58, vertices +0x8C (count), packet buffers +0x94 /
// +0x98 by byte +0x92 (ZOE-Recompiled b6a11d0, GPL-3.0: facts only).
// ZOE_HUD_LOG=<first blank>-<last blank> (diagnosis): each sprite drawn in those blanks, one line.
std::pair<u64, u64> HudLogRange();

// (0x00284870 builds a text-label options list - VIBRATION / CAPTION / SOUND from the table 0x0049B688 and Japanese
// debug lines - which the title never reaches: the title's OPTIONS screen is 0x00420E88's, its rows images.)

// Text objects (0x0045B668: subtitles, captions - its sprite part at +0x20, the sprite's layout 0x20 further on)
// and window containers (0x0045ABE0: panels with children) - logged for now (ZOE_HUD_LOG), placed next.
void HudLogOther(const char* what, u32 object, u32 at) {
    static const std::pair<u64, u64> range = [] {   // ZOE_HUD_LOG_OTHER=<first>-<last> (game frames)
        unsigned long long a = 1, b = 0;
        if (const char* e = std::getenv("ZOE_HUD_LOG_OTHER")) std::sscanf(e, "%llu-%llu", &a, &b);
        return std::make_pair(u64(a), u64(b));
    }();
    if (range.first > range.second || !CameraOk(object)) return;
    const u64 n = Read32(kFrameCounter);
    if (n < range.first || n > range.second) return;
    std::fprintf(stderr, "[hud] frame %llu %s %08X flags %04X pos %.1f,%.1f ax %.3f ay %.3f\n", (unsigned long long)n,
                 what, unsigned(object), unsigned(ps2host::GuestMemory().Read(object + at + 2u, 2)),
                 F32(Read32(object + at + 0x50u)), F32(Read32(object + at + 0x54u)), F32(Read32(object + at + 0x20u)),
                 F32(Read32(object + at + 0x34u)));
}
// Words of guest memory changed for one draw and put back after it (only if still what was written).
struct Changed {
    u32 at[24];
    u32 was[24];
    u32 now[24];
    int n = 0;
    void Put(u32 address, u32 value) {
        if (n == 24) return;
        at[n] = address;
        was[n] = Read32(address);
        now[n] = value;
        Write32(address, value);
        ++n;
    }
    void PutF(u32 address, float value) { Put(address, Bits(value)); }
    void Back() {
        for (int i = n - 1; i >= 0; --i)
            if (Read32(at[i]) == now[i]) Write32(at[i], was[i]);
        n = 0;
    }
};

float WideS() { return (4.0f / 3.0f) / ps2host::WideAspect(); }

// Where x lands for a piece anchored by its centre `centre` (the zones, as HudSprite): the map's offset b in
// x' = S x + b.
float ZoneOffset(float centre, float s) {
    if (!Shows3d()) return 256.0f * (1.0f - s);     // a 2D screen: about the centre
    if (centre < 215.0f) return 0.0f;
    if (centre >= 297.0f) return 512.0f * (1.0f - s);
    return 256.0f * (1.0f - s);
}

// A sprite object's vertex extent in x (its drawn packet buffer: vertices of 0x30 bytes, x at +0x20), false when
// it cannot be read.
bool SpriteExtent(u32 object, float* lo, float* hi) {
    const u32 count = Read32(object + 0x8Cu);
    const u32 buffer = Read32(object + (ps2host::GuestMemory().Read(object + 0x92u, 1) ? 0x94u : 0x98u));
    if (!count || count > 512u || !CameraOk(buffer)) return false;
    *lo = 1e9f;
    *hi = -1e9f;
    for (u32 v = 0; v < count; ++v) {
        const float x = F32(Read32(buffer + v * 0x30u + 0x20u));
        if (!(x > -4096.0f && x < 4096.0f)) return false;
        *lo = std::min(*lo, x);
        *hi = std::max(*hi, x);
    }
    return true;
}

// ---- SUBTITLE LANGUAGE JAPANESE (the maintainer 2026-11-21): the subtitle's text object is the jimaku task's
// (STRM_PLAYJIMAKU) at its +0xA0 - the task's +0x40 its channel's block (count, bytes, 0, 0, then each cue: start,
// end, text bytes, speaker, text), +0x74 / +0x78 the cue shown's start / end (found 2026-11-21 in a RAM dump: block
// 0x6D2100, cue 0x3CC4..0x3D71, the text object 0x6D1E90 at 255, 383). A cue whose text is "#J" + a hash (dub.cpp,
// with the Japanese voices) is drawn by us - its Japanese line in Noto Sans JP, centred where the game's would be -
// and not by the game (whose letters have no Japanese).
u32 g_japanese_subtitle_frame = 0;      // the frame our Japanese subtitle was last drawn (0: none shown)

bool JapaneseSubtitle(u32 object) {
    static const bool chosen = ps2host::Setting("language") == "ja";
    if (!chosen || object < 0xA0u) return false;
    const u32 task = object - 0xA0u;
    const u32 block = Read32(task + 0x40u), start = Read32(task + 0x74u), end = Read32(task + 0x78u);
    if (!CameraOk(block) || !CameraOk(task) || end <= start) return false;
    const u32 count = Read32(block);
    if (!count || count > 512u) return false;
    u32 at = block + 16u;
    std::string text;
    for (u32 n = 0; n < count; ++n) {
        const u32 bytes = Read32(at + 8u);
        if (bytes > 4096u || !CameraOk(at)) return false;
        if (Read32(at) == start && Read32(at + 4u) == end) {
            for (u32 i = 0; i < bytes && i < 64u; ++i) {
                const char ch = char(ps2host::GuestMemory().Read(at + 16u + i, 1));
                if (!ch) break;
                text += ch;
            }
            break;
        }
        at += 16u + bytes;
    }
    const std::string line = DubJapaneseLine(text);
    if (line.empty()) return false;
    std::vector<std::string> rows;
    for (std::size_t from = 0;;) {
        const std::size_t nl = line.find('\n', from);
        rows.push_back(line.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    constexpr float kRowStep = 16.0f, kCaps = 10.0f;            // in the 448 lines: as the game's two rows
    std::vector<ps2host::GameText> texts;
    const float top = 383.0f - kRowStep * float(rows.size()) * 0.5f;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        ps2host::GameText t;
        t.text = rows[r];
        t.x = 0.5f;
        t.centre = true;
        t.y = (top + kRowStep * float(r) + 2.0f) / 448.0f;
        t.height = kCaps / 448.0f;
        t.colour = 0xE6E6E6u;
        texts.push_back(t);
    }
    ps2host::SetGameSubtitleTexts(texts);
    g_japanese_subtitle_frame = Read32(kFrameCounter);
    return true;
}

// 0x0045B668(text object): a text standing alone (messages, subtitles; its sprite part at +0x20, the sprite's
// layout 0x20 on: flags +0x22, axes +0x40 / +0x50, place +0x70). Narrowed by S, kept at its edge - between the
// zones about its own place (centred text stays centred).
void HudText(lifted::Context& c) {
    const u32 object = u32(c.r[4]);
    if (JapaneseSubtitle(object)) return;           // (drawn by us)
    HudLogOther("text", object, 0x20u);
    const float s = WideS();
    if (s >= 0.999f || !CameraOk(object) || !ShowsWide() || (ps2host::GuestMemory().Read(object + 0x22u, 2) & 0x22u)) {
        F_0045B668(c);
        return;
    }
    const float x = F32(Read32(object + 0x70u));
    float to = x;
    // A text's place is its start, not its middle: only near an edge is it the HUD's (the pause's "-PAUSE-",
    // starting left of the middle, went to the left edge - his session 2026-11-21); the rest about the centre.
    if (!Shows3d()) to = 256.0f + s * (x - 256.0f);             // a 2D screen: about the centre
    else if (x < 128.0f) to = s * x;
    else if (x >= 384.0f) to = 512.0f - s * (512.0f - x);
    else to = 256.0f + s * (x - 256.0f);
    Changed changed;
    changed.PutF(object + 0x70u, to);
    changed.PutF(object + 0x40u, F32(Read32(object + 0x40u)) * s);
    changed.PutF(object + 0x50u, F32(Read32(object + 0x50u)) * s);
    F_0045B668(c);
    changed.Back();
}

// 0x0045ABE0(container): a window - its children (list from +0x8C, sentinel +0xA0, next at +0xC; type byte +0: 1
// sprite, 4 text, 2 callback) drawn through its matrix +0x20..+0x5F when flag 0x40 is set, else at their own
// coordinates. With every visible child inheriting the matrix (sprites with 0x4 and none of 0x400 / 0x80 / 0x20,
// texts whose part has 0x4 and not 0x22, the clip callbacks 0x002137A0 / 0x00213800), the whole window is moved by
// one map x' = S x + b on the matrix's x column (b by the clip rectangle's centre, the zones; none: the screen's
// centre) and the clip rectangle with it. A window without its own matrix whose matrix words are the identity gets
// the map as its matrix and the flag for the draw. Left alone: full-screen layers (a child or the clip 90 % of the
// width: the fade / scan-line layers), windows a child does not inherit (the map screen's 3D map window).
// (ZOE-Recompiled b6a11d0, GPL-3.0: facts only.)
constexpr u32 kClipFn = 0x002137A0u, kClipEndFn = 0x00213800u;
// ZOE_MENU_TRACE=1 (diagnosis): each menu label made (0x00282A20(menu, ?, text, ...)), its text and caller.
void MenuLabel(lifted::Context& c) {
    static const bool trace = std::getenv("ZOE_MENU_TRACE") != nullptr;
    if (trace) {
        std::string text;
        const u32 at = u32(c.r[6]);
        for (u32 i = 0; CameraOk(at) && i < 40; ++i) {
            const char ch = char(ps2host::GuestMemory().Read(at + i, 1));
            if (!ch) break;
            text += (ch >= 32 && ch < 127) ? ch : '?';
        }
        std::fprintf(stderr, "[menu] frame %u label a0 %08X a1 %08X text %08X '%s' ra %08X\n", unsigned(Read32(kFrameCounter)),
                     unsigned(c.r[4]), unsigned(c.r[5]), unsigned(at), text.c_str(), unsigned(c.r[31]));
    }
    F_00282A20(c);
}

// ---- The title's OPTIONS screen and RECOMP OPTIONS in it (M7 step 7, M9; the maintainer 2026-11-20: the player's
// settings in the game's own menu, the engine's panel only for debugging). The screen (object a0 of 0x00420E88, its
// state at +0x18000): rows 0 VIBRATION, 1 CAPTION DEMO, 2 CAPTION GAME, 3 SOUND, 4 DEFAULT, 5 EXIT at +0x189F4, a
// move's wait at +0x189EC; its rows are images (sprites through 0x00459908, rows at y 126 / 165 / 190 / 228 / 267 /
// 307 of 448, labels at x 82, values at 275 / 375), 0x00422FF0 moves the cursor (held pad 0x004AA4E0: 0x10 up, 0x40
// down - 6 rows wrapping; left / right change the row's value), 0x00420FA0 acts on a press (0x004AA4D6: 0x2000
// confirm, 0x4000 back), 0x00423240 / 0x004235A0 draw the green frame at the row's place from the table 0x004D1380
// (two sub-rows of four floats per row). The configuration word 0x004DFCA0: bit 0 vibration, 1 captions in the
// demos (the cutscenes - checked: off, the opening's lines are gone), 2 captions in the game, 3 stereo; set through
// the game's own setters (0x004663B0 / 0x00466400 / 0x00466450 / 0x004664A0, a0 on), which mark it for the save.
//
// The maintainer's order (2026-11-20): OPTIONS shows VIBRATION, SOUND, RECOMP OPTIONS, EXIT - CAPTION and DEFAULT
// hidden (their sprites not drawn, the cursor passing their rows), SOUND and EXIT moved up; RECOMP OPTIONS is a
// virtual row (the game on EXIT's row, our flag set). RECOMP OPTIONS opens our pages in the same screen: the game's
// rows hidden, ours drawn by the engine (SetGameTexts: Liberation Sans Bold in the game's colours, its letters
// ~1.2 wider), the green frame moved to our row. Pages: the groups, POST PROCESSING / DISPLAY / PERFORMANCE (FRAME RATE until 2026-11-22) / SHADERS
// / LANGUAGE (SUBTITLES CUTSCENES, SUBTITLES GAMEPLAY - the captions moved here -, VOICES with the Japanese disc,
// SUBTITLE LANGUAGE). Up / down choose, left / right change, circle confirms, cross goes back, triangle sets the
// line back to its default; leaving with a start-only line changed asks RESTART NEEDED.
constexpr u32 kOptionsState = 0x18000u, kOptionsRow = 0x9F4u, kOptionsWait = 0x9ECu;
constexpr u32 kPadHeld = 0x004AA4E0u, kPadPressed = 0x004AA4D6u, kFrameTable = 0x004D1380u;
constexpr u32 kConfigWord = 0x004DFCA0u, kLanguageWord = 0x004DFCA4u;
constexpr u32 kHeldUp = 0x10u, kHeldDown = 0x40u, kHeldLeft = 0x80u, kHeldRight = 0x20u;
constexpr u32 kPressConfirm = 0x2000u, kPressBack = 0x4000u, kPressDefault = 0x1000u;
constexpr int kExitRow = 5, kSoundRow = 3, kVibrationRow = 0;
constexpr float kGameRowY[6] = {126.0f, 165.0f, 190.0f, 228.0f, 267.0f, 307.0f};
constexpr float kSoundY = 165.0f, kRecompY = 204.0f, kExitY = 243.0f;    // OPTIONS' rows after the cleanup
constexpr float kPageTop = 120.0f, kPageStep = 33.0f;                    // our pages' rows
// The game's lettering, measured on its OPTIONS screen: capitals 8-9 of 448 lines, labels grey, the chosen value
// red, the other dark.
constexpr float kLetters = 8.5f, kStretch = 1.2f;
constexpr u32 kGrey = 0x727272u, kRed = 0x9E1F1Fu, kDark = 0x484848u, kTitle = 0x8A8A8Au;
constexpr u32 kWordGrey = kGrey;        // the game's rows' words as shown (white texels times 0x80, drawn in fields)
bool g_recomp_row = false;              // the cursor on RECOMP OPTIONS
u32 g_options_seen = 0;                 // the game frame the OPTIONS screen last ran in
u32 g_options_obj = 0;

bool PageOpen();                        // (our pages, below)
void PageClosed();

// A game sound (0x00461ED0(id, 0x3F, 0x20): 0x39 the cursor's move, 0x3A a confirm), the caller's registers kept.
void GameSound(lifted::Context& c, u32 id) {
    u64 saved[5], saved_hi[5];
    const int regs[5] = {4, 5, 6, 7, 31};
    for (int i = 0; i < 5; ++i) saved[i] = c.r[regs[i]], saved_hi[i] = c.rh[regs[i]];
    c.r[4] = id;
    c.r[5] = 0x3F;
    c.r[6] = 0x20;
    F_00461ED0(c);
    for (int i = 0; i < 5; ++i) c.r[regs[i]] = saved[i], c.rh[regs[i]] = saved_hi[i];
}

// One of the game's configuration setters (a0: on), the caller's registers kept.
void GameSetter(lifted::Context& c, void (*setter)(lifted::Context&), bool on) {
    u64 saved[5], saved_hi[5];
    const int regs[5] = {2, 3, 4, 5, 31};
    for (int i = 0; i < 5; ++i) saved[i] = c.r[regs[i]], saved_hi[i] = c.rh[regs[i]];
    c.r[4] = on ? 1 : 0;
    setter(c);
    for (int i = 0; i < 5; ++i) c.r[regs[i]] = saved[i], c.rh[regs[i]] = saved_hi[i];
}

u32 GameLanguage();

}  // namespace
bool ReadZoeDatFile(const std::string& path, std::vector<unsigned char>* out);     // dub.cpp
namespace {

// ---- Our pages. A line: a group (opens its page), an engine setting (PcSettings' index), a caption bit of the
// configuration word, EXIT, or RESTART NEEDED's two.
enum Page { kNoPage = -1, kTopPage, kPostPage, kDisplayPage, kRatePage, kShaderPage, kLanguagePage, kRestartPage };
enum LineKind { kGroup, kSetting, kCaption, kExitLine, kRestartNow, kRestartLater, kButtonMapping };
struct Line {
    LineKind kind;
    std::string label;
    int arg = 0;                        // the page, the setting's index, the caption's bit
    std::string value;                  // shown
    std::vector<std::string> values;    // two: shown side by side as the game's own (ON / OFF)
};
int g_page = kNoPage;
int g_page_row = 0;
int g_page_wait = 0;
bool g_frame_moved = false;             // the green frame to be built again (the screen's +0x9F8, as a row move sets)
u32 g_sound_drawn = 0;                  // the frame SOUND's word was last drawn in, its fade and its slide
float g_sound_alpha = 1.0f, g_sound_dx = 0.0f;
u32 g_stereo_drawn = 0;                 // and STEREO's (the red word passing over SOUND on the way in), its place
float g_stereo_x = 259.0f;
// The start-only lines as this run started (taken when the pages first opened): RESTART NEEDED comes at every
// leave of the pages while they differ, not once per change (2026-11-20 issue 4).
std::vector<std::pair<std::string, std::string>> g_restart_seen;
bool g_restart_taken = false;

const char* PageName(int page) {
    switch (page) {
        case kPostPage: return "POST PROCESSING";
        case kDisplayPage: return "DISPLAY";
        case kRatePage: return "PERFORMANCE";
        case kShaderPage: return "SHADERS";
        case kLanguagePage: return "LANGUAGE";
        case kRestartPage: return "RESTART NEEDED";
        default: return "";
    }
}

// The engine's lines for a page (by its panel page or, for DISPLAY and SHADERS, by name).
int PageOfSetting(const ps2host::PcSetting& s) {
    if (s.page == "POST PROCESSING") return kPostPage;
    if (s.page == "PERFORMANCE") return kRatePage;
    if (s.label == "DISPLAY MODE" || s.label == "RENDER RESOLUTION" || s.label == "ASPECT RATIO" ||
        s.label == "STRETCH CUTSCENES" || s.label == "FIELD OF VIEW")
        return kDisplayPage;
    if (s.label == "SHADER COMPILE" || s.label == "SHADER NOTICE") return kShaderPage;
    if (s.label == "LANGUAGE" || s.label == "DUB") return kLanguagePage;
    return kTopPage;                    // TEXTURES (with a pack)
}

std::vector<Line> PageLines(int page) {
    std::vector<Line> lines;
    const std::vector<ps2host::PcSetting> settings = ps2host::PcSettings();
    auto setting = [&](int i) {
        const ps2host::PcSetting& s = settings[std::size_t(i)];
        std::string label = s.label;
        if (label == "DUB") label = "VOICES";
        if (label == "LANGUAGE") label = "SUBTITLE LANGUAGE";
        // Two values side by side (as SOUND's STEREO MONAURAL); FSR PRESET's three too (the maintainer 2026-11-21:
        // all three shown, left / right between them).
        const bool row = s.values.size() == 2 || (s.values.size() == 3 && s.label == "FSR PRESET") ||
                         (s.values.size() == 4 && s.label == "FSR SHARPNESS");
        lines.push_back({kSetting, label, i, s.value, row ? s.values : std::vector<std::string>{}});
    };
    if (page == kTopPage) {
        // DISPLAY first, then POST PROCESSING (the maintainer 2026-11-20).
        for (int group : {kDisplayPage, kPostPage, kRatePage, kShaderPage}) {
            bool any = false;
            for (const auto& s : settings) any = any || PageOfSetting(s) == group;
            if (any) lines.push_back({kGroup, PageName(group), group, "", {}});
        }
        for (int i = 0; i < int(settings.size()); ++i)
            if (PageOfSetting(settings[std::size_t(i)]) == kTopPage) setting(i);
        lines.push_back({kGroup, PageName(kLanguagePage), kLanguagePage, "", {}});
        // BUTTON MAPPING (the maintainer 2026-11-21): the engine's page (device, RESET TO DEFAULT, each input) -
        // a player's only way to it now that the F9 panel is internal.
        lines.push_back({kButtonMapping, "BUTTON MAPPING", 0, "", {}});
    } else if (page == kLanguagePage) {
        const u32 config = Read32(kConfigWord);
        lines.push_back({kCaption, "SUBTITLES CUTSCENES", 1, (config & 2u) ? "ON" : "OFF", {"ON", "OFF"}});
        lines.push_back({kCaption, "SUBTITLES GAMEPLAY", 2, (config & 4u) ? "ON" : "OFF", {"ON", "OFF"}});
        for (int i = 0; i < int(settings.size()); ++i)
            if (settings[std::size_t(i)].label == "DUB") setting(i);
        for (int i = 0; i < int(settings.size()); ++i)
            if (settings[std::size_t(i)].label == "LANGUAGE") setting(i);
    } else if (page == kRestartPage) {
        lines.push_back({kRestartNow, "RESTART NOW", 0, "", {}});
        lines.push_back({kRestartLater, "LATER", 0, "", {}});
        return lines;
    } else {
        for (int i = 0; i < int(settings.size()); ++i)
            if (PageOfSetting(settings[std::size_t(i)]) == page) setting(i);
    }
    // (No EXIT line: Cross is back on every page - the hint says so; the maintainer 2026-11-21.)
    return lines;
}

bool PageOpen() { return g_page != kNoPage; }

// The start-only lines (VOICES: the disc is mapped at the start; SUBTITLE LANGUAGE is taken at once here).
std::vector<std::pair<std::string, std::string>> RestartLines() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& s : ps2host::PcSettings())
        if (s.restart && s.label != "LANGUAGE") out.push_back({s.label, s.value});
    return out;
}

void OpenPage(int page, int row = 0) {
    if (!g_restart_taken) {
        g_restart_seen = RestartLines();
        g_restart_taken = true;
    }
    g_page = page;
    g_page_row = row;
    g_page_wait = 10;
    g_frame_moved = true;
}

void PageClosed() {
    if (g_page != kNoPage) g_frame_moved = true;
    g_page = kNoPage;
    g_page_row = 0;
}

// Back: a page to the groups (on its own row), the groups to OPTIONS - by RESTART NEEDED when a start-only line
// changed.
void PageBack() {
    if (g_page == kTopPage) {
        if (RestartLines() != g_restart_seen) {
            OpenPage(kRestartPage, 1);
            return;
        }
        PageClosed();
        return;
    }
    if (g_page == kRestartPage) {
        PageClosed();
        return;
    }
    const int from = g_page;
    const std::vector<Line> top = PageLines(kTopPage);
    int row = 0;
    for (int i = 0; i < int(top.size()); ++i)
        if (top[std::size_t(i)].kind == kGroup && top[std::size_t(i)].arg == from) row = i;
    OpenPage(kTopPage, row);
}

// A subtitles caption (1 SUBTITLES CUTSCENES, 2 SUBTITLES GAMEPLAY) as the game's own setters 0x00466400 /
// 0x00466450 set it - their whole work: the bit in the configuration word, then 1 at 0x004AA930 - written here,
// so that SELECT's SETTINGS can set it with the game held (no guest call).
void SetCaption(u32 bit, bool on) {
    const u32 word = Read32(kConfigWord);
    ps2host::GuestMemory().Write(kConfigWord, on ? (word | (1u << bit)) : (word & ~(1u << bit)), 4);
    ps2host::GuestMemory().Write(0x004AA930u, 1u, 4);
}

void ChangeLine(const Line& line, int step) {
    if (line.kind == kSetting) {
        ps2host::StepPcSetting(line.arg, step);
        if (line.label == "SUBTITLE LANGUAGE") ps2host::GuestMemory().Write(kLanguageWord, GameLanguage(), 4);
    } else if (line.kind == kCaption) {
        SetCaption(u32(line.arg), !(Read32(kConfigWord) & (1u << line.arg)));
    }
}

void DefaultLine(const Line& line) {
    if (line.kind == kSetting) {
        ps2host::ResetPcSetting(line.arg);
        if (line.label == "SUBTITLE LANGUAGE") ps2host::GuestMemory().Write(kLanguageWord, GameLanguage(), 4);
    } else if (line.kind == kCaption) {
        SetCaption(u32(line.arg), true);
    }
}

// The pages' input: each frame the OPTIONS screen runs (its own cursor and press calls skipped meanwhile; `c` the
// game's, for its menu sounds), or each window pump while SELECT's SETTINGS holds the game (no context: silent).
// `held` the pad's directions (0x10 up ... 0x80 left), `pressed` its new presses (0x1000 triangle, 0x2000 circle,
// 0x4000 cross).
void PageInput(lifted::Context* c, u32 held, u32 pressed) {
    const std::vector<Line> lines = PageLines(g_page);
    const int n = int(lines.size());
    if (n == 0) {                           // (a page with no line: Cross back alone)
        if ((pressed & 0x7000u) == kPressBack) PageBack();
        return;
    }
    if (g_page_row >= n) g_page_row = n - 1;
    held &= 0xF0u;
    pressed &= 0x7000u;
    auto sound = [&](u32 id) {
        if (c) GameSound(*c, id);
    };
    if (g_page_wait > 0) --g_page_wait;
    const Line& line = lines[std::size_t(g_page_row)];
    if (g_page_wait == 0 && (held == kHeldUp || held == kHeldDown)) {
        g_page_row = (g_page_row + (held == kHeldDown ? 1 : n - 1)) % n;
        g_page_wait = 10;
        g_frame_moved = true;
        sound(0x39);
    } else if (g_page_wait == 0 && (held == kHeldLeft || held == kHeldRight) &&
               (line.kind == kSetting || line.kind == kCaption)) {
        ChangeLine(line, held == kHeldRight ? 1 : -1);
        g_page_wait = 10;
        sound(0x39);
    } else if (pressed == kPressConfirm && g_page_wait == 0) {
        sound(0x3A);
        if (line.kind == kGroup) OpenPage(line.arg);
        else if (line.kind == kButtonMapping) ps2host::OpenPcMenuControls();
        else if (line.kind == kExitLine || line.kind == kRestartLater) PageBack();
        else if (line.kind == kRestartNow) ps2host::RequestRestart();
        else ChangeLine(line, 1);
        g_page_wait = 10;
    } else if (pressed == kPressBack) {
        sound(0x3A);
        PageBack();
        g_page_wait = 10;
    } else if (pressed == kPressDefault && (line.kind == kSetting || line.kind == kCaption)) {
        DefaultLine(line);
        sound(0x39);
    }
}

// RECOMP OPTIONS in the game's own menu lettering (the maintainer 2026-11-20: rebuilt from the menu font's own
// letters - these two words only, the pages keep Liberation Sans): its letters cut from the OPTIONS rows' word
// images in the player's own stage/title/title.pak (ZOE.DAT) - each word a 4-bit image (`stride` = its width
// rounded up to 8, 11 rows, a 48-byte head before it giving width and height at +8 / +10) with its 16-entry CLUT
// right after it (16-byte aligned; alpha 0..127 the coverage, colour white) - and set side by side as cut, at the
// columns where the game's own letters meet (the lowest coverage between them). Nothing of the game's is kept in
// this source: offsets and columns only.
struct Glyph {
    u32 word;                   // the word image's offset in title.pak
    u32 width;                  // its width (its head says so: checked)
    u32 from, to;               // the letter's columns
};
constexpr u32 kCaptionWord = 0x20260u, kDemoWord = 0x20680u, kExitWord = 0x20810u, kSoundWord = 0x20FE0u,
              kStereoWord = 0x211B0u, kVibrationWord = 0x213B0u;
constexpr u32 kWordRows = 11;

std::vector<std::uint8_t> WordMask(const std::vector<unsigned char>& pak, u32 at, u32 width) {
    if (at < 48 || at >= pak.size()) return {};
    const unsigned char* head = &pak[at - 48];
    if ((head[8] | head[9] << 8) != width || head[10] < kWordRows) return {};
    const u32 stride = (width + 7) / 8 * 8, bytes = (stride * head[10] / 2 + 15) / 16 * 16;
    if (at + bytes + 64 > pak.size()) return {};
    const unsigned char* clut = &pak[at + bytes];
    std::vector<std::uint8_t> mask(std::size_t(width) * kWordRows);
    for (u32 y = 0; y < kWordRows; ++y)
        for (u32 x = 0; x < width; ++x) {
            const u32 i = y * stride + x;
            const u32 index = (pak[at + i / 2] >> ((i & 1) * 4)) & 15u;
            mask[std::size_t(y) * width + x] = std::uint8_t(std::min(255u, u32(clut[index * 4 + 3]) * 2u));
        }
    return mask;
}

// The masks (11 rows each) of RECOMP OPTIONS and of EXIT (the game's own word, drawn by us so that both rows
// follow SOUND's animation), empty when the disc's file is not as expected (then the engine's letters / the game's
// EXIT).
struct WordMasks {
    std::vector<std::uint8_t> recomp, exit;
    u32 recomp_w = 0, exit_w = 0;
};
const WordMasks& OptionsMasks() {
    static WordMasks masks;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::vector<unsigned char> pak;
        bool ok = ReadZoeDatFile("stage/title/title.pak", &pak);
        std::vector<std::uint8_t>& built = masks.recomp;
        u32& built_width = masks.recomp_w;
        if (ok) {
            masks.exit = WordMask(pak, kExitWord, 32);
            masks.exit_w = masks.exit.empty() ? 0 : 32;
        }
        // R, E, T from STEREO; C, P from CAPTION; O, N, S from SOUND; M from DEMO; I from VIBRATION - all with their
        // ink on rows 1-10 (EXIT's T sits a row higher: not used).
        const Glyph r{kStereoWord, 56, 25, 35}, e{kStereoWord, 56, 17, 24}, cc{kCaptionWord, 60, 0, 10},
            o{kSoundWord, 48, 9, 19}, m{kDemoWord, 40, 17, 28}, p{kCaptionWord, 60, 19, 28}, t{kStereoWord, 56, 10, 17},
            i{kVibrationWord, 72, 9, 13}, n{kSoundWord, 48, 28, 38}, s{kSoundWord, 48, 0, 9};
        const std::vector<const Glyph*> letters = {&r, &e, &cc, &o, &m, &p, nullptr, &o, &p, &t, &i, &o, &n, &s};
        constexpr u32 kSpace = 4;
        std::vector<std::vector<std::uint8_t>> columns;     // the result's columns
        for (const Glyph* g : letters) {
            if (!ok) break;
            if (!g) {
                for (u32 k = 0; k < kSpace; ++k) columns.emplace_back(kWordRows, 0);
                continue;
            }
            const std::vector<std::uint8_t> word = WordMask(pak, g->word, g->width);
            if (word.empty()) {
                ok = false;
                break;
            }
            for (u32 x = g->from; x < g->to; ++x) {
                std::vector<std::uint8_t> column(kWordRows);
                for (u32 y = 0; y < kWordRows; ++y) column[y] = word[std::size_t(y) * g->width + x];
                columns.push_back(column);
            }
        }
        if (ok && !columns.empty()) {
            built_width = u32(columns.size());
            built.assign(std::size_t(built_width) * kWordRows, 0);
            for (u32 x = 0; x < built_width; ++x)
                for (u32 y = 0; y < kWordRows; ++y) built[std::size_t(y) * built_width + x] = columns[x][y];
        }
        std::fprintf(stderr, "[menu] RECOMP OPTIONS %s\n",
                     ok ? "in the game's own letters (title.pak)" : "in the engine's letters (title.pak not as expected)");
    }
    return masks;
}

}  // namespace
// Called when the disc is open (dub.cpp's hook, before the game starts): the words read then.
void WarmOptionsMenu() { OptionsMasks(); }
namespace {

// A mask at a row of the OPTIONS screen, placed as the game places its rows' words (a texel a column of 512, 1.07
// lines a row of 448, the first texel's centre half a texel in), in `colour`, faded by `alpha`.
ps2host::GameText MaskLabel(const std::vector<std::uint8_t>& mask, u32 width, float x, float y, u32 colour,
                            float alpha) {
    ps2host::GameText label;
    label.mask = mask;
    if (alpha < 1.0f)
        for (std::uint8_t& m : label.mask) m = std::uint8_t(m * alpha + 0.5f);
    label.mask_w = width;
    label.mask_h = kWordRows;
    label.x = x / 512.0f;
    label.y = (y - 0.5f) / 448.0f;
    label.width = float(width) / 512.0f;
    label.height = float(kWordRows) * 1.07f / 448.0f;
    label.colour = colour;
    return label;
}

// The texts of the OPTIONS screen this frame: RECOMP OPTIONS among the game's rows, or our page.
bool g_select_settings = false;     // our pages opened from SELECT's SETTINGS (the game held), not the OPTIONS screen

// The buttons' small signs for the hint (16 x 16 coverage): 0 circle, 1 cross, 2 triangle - drawn, not letters
// (the font has none of them).
std::vector<std::uint8_t> ButtonMask(int kind) {
    std::vector<std::uint8_t> m(256, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const float fx = float(x) + 0.5f - 8.0f, fy = float(y) + 0.5f - 8.0f;
            float d = 9.0f;
            if (kind == 0) {
                d = std::fabs(std::sqrt(fx * fx + fy * fy) - 6.0f);
            } else if (kind == 1) {
                d = std::min(std::fabs(fx - fy), std::fabs(fx + fy)) * 0.7071f;
                if (std::fabs(fx) > 6.0f || std::fabs(fy) > 6.0f) d = 9.0f;
            } else {
                const float top = -6.0f, bottom = 5.5f, half = (fy - top) / (bottom - top) * 6.5f;
                if (fy >= top - 1.0f && fy <= bottom + 1.0f)
                    d = std::min(std::fabs(std::fabs(fx) - half) * 0.85f, std::fabs(fy - bottom));
                if (std::fabs(fx) > half + 1.5f) d = 9.0f;
            }
            const float a = std::clamp(1.6f - d, 0.0f, 1.0f);
            m[std::size_t(y) * 16 + std::size_t(x)] = std::uint8_t(a * 255.0f);
        }
    return m;
}

// The small hint at the bottom right (the maintainer 2026-11-20): "△ DEFAULT" (on the setting pages) over
// "○ SELECT  × BACK", in the menu's grey, half the rows' height.
void AddHints(std::vector<ps2host::GameText>* texts, bool with_default) {
    constexpr float kSmall = kLetters * 0.75f, kSign = 7.0f;
    constexpr u32 kHint = 0x808080u;
    auto sign = [&](int kind, float x, float y) {
        ps2host::GameText g;
        g.mask = ButtonMask(kind);
        g.mask_w = g.mask_h = 16;
        g.x = x / 512.0f;
        g.y = y / 448.0f;
        g.width = kSign / 512.0f;
        g.height = kSign * 1.07f / 448.0f;
        g.colour = kHint;
        texts->push_back(g);
    };
    auto word = [&](const std::string& s, float x, float y) {
        texts->push_back(ps2host::GameText{s, x / 512.0f, (y + 1.0f) / 448.0f, kSmall / 448.0f, kHint, false, kStretch});
    };
    // "○ SELECT  × BACK", and "△ DEFAULT" under the cross, flush with it (the maintainer 2026-11-21).
    // (Inside the OPTIONS screen's frame: left of its upright green line at x 412, between its grey line at 338 and
    // the green one at 395 - not crossed by them; his shot 2026-11-21.)
    sign(0, 300.0f, 352.0f);
    word("SELECT", 310.0f, 352.0f);
    sign(1, 356.0f, 352.0f);
    word("BACK", 366.0f, 352.0f);
    if (with_default) {
        sign(2, 356.0f, 364.0f);
        word("DEFAULT", 366.0f, 364.0f);
    }
}

// Our words on a 2D screen shown wide (variant B): placed as HudSprite places the game's own there - x about the
// centre by S, a mask's width by S (the letters of a text are already drawn square) - so they line up with the
// game's rows again (his session 2026-11-20: shifted, not indented as the game's).
void SetPlacedTexts(std::vector<ps2host::GameText> texts) {
    const float s = WideS();
    if (s < 0.999f && ShowsWide() && !Shows3d())
        for (ps2host::GameText& t : texts) {
            if (!t.mask.empty() && t.width >= 0.999f) continue;     // a ground over the whole picture stays so
            t.x = (256.0f + s * (t.x * 512.0f - 256.0f)) / 512.0f;
            if (!t.mask.empty()) t.width *= s;
        }
    ps2host::SetGameTexts(texts);
}

void OptionsTexts() {
    if (ps2host::PcPanelOpen()) return;     // BUTTON MAPPING (ControlsLook draws it) over ours meanwhile
    auto text = [](const std::string& s, float x, float y, u32 colour) {
        return ps2host::GameText{s, x / 512.0f, (y + 2.0f) / 448.0f, kLetters / 448.0f, colour, false, kStretch};
    };
    std::vector<ps2host::GameText> texts;
    if (!PageOpen()) {
        // RECOMP OPTIONS and EXIT with SOUND's animation (the maintainer 2026-11-20): shown while SOUND's word is
        // drawn, at its slide (x -80 -> 82, 16 a frame) and its fade; and as SOUND has its red STEREO passing over
        // it on the way in, a red copy of each word rides with STEREO until STEREO is in its place (x 259).
        const u32 now = Read32(kFrameCounter);
        const bool shown = g_sound_drawn && now - g_sound_drawn <= 1 && g_sound_alpha > 0.0f;
        if (!shown) {
            ps2host::SetGameTexts({});
            return;
        }
        const WordMasks& masks = OptionsMasks();
        // The game's words wait at x -80 (off the screen) until their row slides: a word wider than the one it
        // follows starts further left by the difference, so that it too waits unseen (RECOMP OPTIONS' end showed
        // at the left edge, waiting - the maintainer 2026-11-20), and gets to its place in the same frames.
        auto along = [](float x, float from, float to, u32 width, float followed) {
            const float p = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
            return x - std::max(0.0f, float(width) - followed) * (1.0f - p);
        };
        const float sound_x = 82.0f + g_sound_dx;
        const bool ghost = g_stereo_drawn && now - g_stereo_drawn <= 1 && g_stereo_x < 258.5f;
        if (!masks.recomp.empty()) {
            if (ghost)
                texts.push_back(MaskLabel(masks.recomp, masks.recomp_w,
                                          along(g_stereo_x, -80.0f, 259.0f, masks.recomp_w, 56.0f), kRecompY, kRed,
                                          g_sound_alpha));
            texts.push_back(MaskLabel(masks.recomp, masks.recomp_w, along(sound_x, -80.0f, 82.0f, masks.recomp_w, 48.0f),
                                      kRecompY, kWordGrey, g_sound_alpha));
        } else {
            texts.push_back(text("RECOMP OPTIONS", sound_x, kRecompY, kGrey));
        }
        if (!masks.exit.empty()) {
            if (ghost)
                texts.push_back(MaskLabel(masks.exit, masks.exit_w, along(g_stereo_x, -80.0f, 259.0f, masks.exit_w, 56.0f),
                                          kExitY, kRed, g_sound_alpha));
            texts.push_back(MaskLabel(masks.exit, masks.exit_w, along(sound_x, -80.0f, 82.0f, masks.exit_w, 48.0f),
                                      kExitY, kWordGrey, g_sound_alpha));
        }
        SetPlacedTexts(texts);
        return;
    }
    if (g_select_settings) {                // the game held behind SELECT's SETTINGS: a dark ground under the page
        ps2host::GameText ground;
        ground.mask.assign(16, 225);
        ground.mask_w = ground.mask_h = 4;
        ground.x = 0.0f;
        ground.y = 0.0f;
        ground.width = 1.0f;
        ground.height = 1.0f;
        ground.colour = 0x000000u;
        texts.push_back(ground);
    }
    const std::string head = g_select_settings ? "SETTINGS" : "RECOMP OPTIONS";
    texts.push_back(text(g_page == kTopPage ? head
                                            : g_page == kRestartPage ? std::string(PageName(g_page))
                                                                     : head + " - " + PageName(g_page),
                         82.0f, 66.0f, kTitle));
    AddHints(&texts, g_page != kRestartPage);
    const std::vector<Line> lines = PageLines(g_page);
    for (int i = 0; i < int(lines.size()); ++i) {
        const Line& l = lines[std::size_t(i)];
        const float y = kPageTop + kPageStep * float(i);
        // SELECT's SETTINGS: no game frame shows the row there - ours, as SELECT's own (a green band, the label bright).
        const bool picked = g_select_settings && i == g_page_row;
        if (picked) {
            ps2host::GameText band;
            band.mask.assign(16, 60);
            band.mask_w = band.mask_h = 4;
            band.x = 72.0f / 512.0f;
            band.y = (y - 3.0f) / 448.0f;
            band.width = 368.0f / 512.0f;
            band.height = 16.0f / 448.0f;
            band.colour = 0x2CC488u;
            texts.push_back(band);
        }
        texts.push_back(text(l.label, 82.0f, y, picked ? 0x46F098u : kGrey));
        if (l.values.size() == 2) {
            texts.push_back(text(l.values[0], 275.0f, y, l.value == l.values[0] ? kRed : kDark));
            texts.push_back(text(l.values[1], 375.0f, y, l.value == l.values[1] ? kRed : kDark));
        } else if (l.values.size() == 3 || l.values.size() == 4) {
            // FSR PRESET's three, FSR SHARPNESS's four (the maintainer 2026-11-21: all side by side)
            static const float kAt3[3] = {230.0f, 320.0f, 410.0f}, kAt4[4] = {230.0f, 290.0f, 370.0f, 430.0f};
            const float* at = l.values.size() == 3 ? kAt3 : kAt4;
            for (std::size_t k = 0; k < l.values.size(); ++k)
                texts.push_back(text(l.values[k], at[k], y, l.value == l.values[k] ? kRed : kDark));
        } else if (!l.value.empty()) {
            texts.push_back(text(l.value, 275.0f, y, kRed));
        }
    }
    SetPlacedTexts(texts);
}

// A sprite of the OPTIONS screen's rows: hidden (true), or moved down by *dy (its y changed for the draw).
bool OptionsSprite(u32 object, float* dy) {
    *dy = 0;
    if (!g_options_seen || Read32(kFrameCounter) - g_options_seen > 1 || !CameraOk(object)) return false;
    // The rows' words slide in from x -80 (16 a frame) - matched by their row (y) and texture width alone, wherever
    // they are; a whole-width sprite (a line, a layer) is not a row's word.
    const float x = F32(Read32(object + 0x50u)), y = F32(Read32(object + 0x54u));
    if (y < 118.0f || y > 320.0f) return false;
    const u32 buffer = Read32(object + (ps2host::GuestMemory().Read(object + 0x92u, 1) ? 0x94u : 0x98u));
    const float w = CameraOk(buffer) ? F32(Read32(buffer + 0x50u)) : 0.0f;
    if (!(w > 0.0f && w < 200.0f)) return false;
    auto near = [&](float at) { return std::fabs(y - at) < 4.0f; };
    if (near(kGameRowY[kSoundRow])) {
        // SOUND's word (48 wide) and STEREO (56): RECOMP OPTIONS and EXIT follow them (OptionsTexts) - the
        // packet's vertex alpha at +0x1C (128 -> 0 over 10 frames on leaving), the place's x.
        const u32 now = Read32(kFrameCounter);
        if (std::fabs(w - 48.0f) < 0.5f) {
            g_sound_drawn = now;
            g_sound_alpha = std::clamp(F32(Read32(buffer + 0x1Cu)) / 128.0f, 0.0f, 1.0f);
            g_sound_dx = x - 82.0f;
        } else if (std::fabs(w - 56.0f) < 0.5f) {
            g_stereo_drawn = now;
            g_stereo_x = x;
        }
    }
    if (PageOpen()) return true;                                    // our page: the game's rows away
    if (near(kGameRowY[1]) || near(kGameRowY[2]) || near(kGameRowY[4])) return true;     // CAPTION, DEFAULT
    if (near(kGameRowY[kExitRow])) {
        if (!OptionsMasks().exit.empty()) return true;              // ours, with SOUND's animation
        *dy = kExitY - kGameRowY[kExitRow];
    }
    if (near(kGameRowY[kSoundRow])) *dy = kSoundY - kGameRowY[kSoundRow];
    return false;
}

// SELECT's menu (issue 3, the maintainer 2026-11-20: in the middle of the screen, as if the game's own) in the
// look of the game's pause menu (WEAPONS ... SAVE): a dark see-through panel in a green frame with a thicker bar
// along the top-left, the rows in the menu's letters - the chosen one bright green, the others pale grey.
void SelectMenuLook(bool open, int row) {
    if (!open) {
        ps2host::SetGameTexts({});
        return;
    }
    constexpr float kX0 = 166.0f, kY0 = 160.0f, kW = 180.0f, kH = 116.0f;    // the panel, in the 512 x 448 picture
    constexpr u32 kGreen = 0x2CC488u, kPicked = 0x46F098u, kPale = 0xC8C8C8u;
    auto box = [](float x, float y, float w, float h, u32 colour, std::uint8_t cover) {
        ps2host::GameText g;
        g.mask_w = 4;
        g.mask_h = 4;
        g.mask.assign(16, cover);
        g.x = x / 512.0f;
        g.y = y / 448.0f;
        g.width = w / 512.0f;
        g.height = h / 448.0f;
        g.colour = colour;
        return g;
    };
    auto text = [](const std::string& s, float x, float y, u32 colour) {
        return ps2host::GameText{s, x / 512.0f, (y + 2.0f) / 448.0f, kLetters / 448.0f, colour, false, kStretch};
    };
    std::vector<ps2host::GameText> texts;
    texts.push_back(box(kX0, kY0, kW, kH, 0x000000u, 176));                  // the panel
    texts.push_back(box(kX0, kY0, kW, 1.5f, kGreen, 255));                   // the frame
    texts.push_back(box(kX0, kY0 + kH - 1.5f, kW, 1.5f, kGreen, 255));
    texts.push_back(box(kX0, kY0, 1.5f, kH, kGreen, 255));
    texts.push_back(box(kX0 + kW - 1.5f, kY0, 1.5f, kH, kGreen, 255));
    texts.push_back(box(kX0, kY0 - 3.0f, kW * 0.45f, 4.0f, kGreen, 255));    // the top-left bar
    texts.push_back(box(kX0 + kW * 0.55f, kY0 + kH - 1.0f, kW * 0.45f, 4.0f, kGreen, 255));  // and bottom-right
    static const char* const kRows[4] = {"RESUME", "SETTINGS", "EXIT GAME", "EXIT TO MENU"};
    if (row >= 4) {
        // An exit asked first (the maintainer 2026-11-21): the question, the warning, YES and NO side by side.
        const bool yes = (row - 4) % 2 == 0;
        texts.push_back(text(std::string(kRows[2 + (row - 4) / 2]) + "?", kX0 + 18.0f, kY0 + 16.0f, kPale));
        texts.push_back(text("UNSAVED PROGRESS", kX0 + 18.0f, kY0 + 40.0f, kGrey));
        texts.push_back(text("WILL BE LOST.", kX0 + 18.0f, kY0 + 56.0f, kGrey));
        const float y = kY0 + 88.0f;
        texts.push_back(box(kX0 + (yes ? 12.0f : 96.0f), y - 3.0f, 72.0f, 16.0f, kGreen, 60));
        texts.push_back(text("YES", kX0 + 30.0f, y, yes ? kPicked : kPale));
        texts.push_back(text("NO", kX0 + 118.0f, y, yes ? kPale : kPicked));
        AddHints(&texts, false);
        ps2host::SetGameTexts(texts);
        return;
    }
    for (int r = 0; r < 4; ++r) {
        const float y = kY0 + 16.0f + 24.0f * float(r);
        if (r == row) texts.push_back(box(kX0 + 8.0f, y - 3.0f, kW - 16.0f, 16.0f, kGreen, 60));   // the row's band
        texts.push_back(text(kRows[r], kX0 + 18.0f, y, r == row ? kPicked : kPale));
    }
    AddHints(&texts, false);
    ps2host::SetGameTexts(texts);
}

// ---- The title's main menu with EXIT GAME (the maintainer 2026-11-21). The game's menu is a wheel of three
// (0x00414B68, its object's +0x5C10 the row shown, +0x5C18 the row moving to, +0x5C14 their number 3, +0x5C1C the
// move's frames): 0 NEW GAME, 1 LOAD GAME, 2 OPTIONS - the chosen one in the middle (y 333, bright, with its glow),
// the one before above (307), the one after below (359). Ours has four - NEW GAME, LOAD GAME, OPTIONS, EXIT GAME
// (after OPTIONS: the maintainer 2026-11-21) - drawn whole by us from the game's own word images (title.pak; EXIT +
// GAME side by side), rolling as the game's does (6 frames a row, the neighbours dim); the game's words and glow
// hidden meanwhile (TitleSprite). The game's cursor stands on ours (NEW / LOAD / OPTIONS), so its confirm is its own.
enum TitleRow { kTitleNew = 0, kTitleLoad = 1, kTitleOptions = 2, kTitleExit = 3 };
int g_title_row = kTitleNew;            // ours: the row chosen
float g_title_pos = 0.0f;               // the wheel's place as drawn (rolls to g_title_row)
u32 g_title_seen = 0;                   // the frame counter when the menu last ran (0x00414B68)
bool g_title_drawn = false;             // our words on the screen
u32 g_title_confirmed = 0;              // the frame a row was chosen (the game's own flash of it shown, not ours)
constexpr float kTitleMid = 333.0f, kTitleStep = 26.0f;
constexpr u32 kNewGameWord = 0x167850u, kLoadGameWord = 0x166B20u, kOptionsWord = 0x168F10u, kGameWord = 0x20970u;
constexpr u32 kTitleBright = 0xC0C0C0u, kTitleDim = 0x6C6C6Cu;

struct TitleMasks {
    std::vector<std::uint8_t> word[4];  // NEW GAME, LOAD GAME, OPTIONS, EXIT GAME
    u32 w[4] = {};
};
const TitleMasks& TitleWordMasks() {
    static TitleMasks m;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::vector<unsigned char> pak;
        if (ReadZoeDatFile("stage/title/title.pak", &pak)) {
            const u32 at[3] = {kNewGameWord, kLoadGameWord, kOptionsWord}, width[3] = {80, 88, 64};
            for (int i = 0; i < 3; ++i) {
                m.word[i] = WordMask(pak, at[i], width[i]);
                m.w[i] = m.word[i].empty() ? 0 : width[i];
            }
            const std::vector<std::uint8_t> exit = WordMask(pak, kExitWord, 32), game = WordMask(pak, kGameWord, 40);
            if (!exit.empty() && !game.empty()) {
                constexpr u32 kGap = 3;             // a space as the game's own two-word images have it
                m.w[3] = 32 + kGap + 40;
                m.word[3].assign(std::size_t(m.w[3]) * kWordRows, 0);
                for (u32 y = 0; y < kWordRows; ++y) {
                    for (u32 x = 0; x < 32; ++x) m.word[3][y * m.w[3] + x] = exit[y * 32 + x];
                    for (u32 x = 0; x < 40; ++x) m.word[3][y * m.w[3] + 32 + kGap + x] = game[y * 40 + x];
                }
            }
        }
    }
    return m;
}
bool TitleReady() {
    static const bool off = std::getenv("ZOE_TITLE_GAME") != nullptr;     // (diagnosis: the game's own wheel)
    const TitleMasks& m = TitleWordMasks();
    return !off && m.w[0] && m.w[1] && m.w[2] && m.w[3];
}

bool TitleActive() { return g_title_seen && Read32(kFrameCounter) - g_title_seen <= 2u; }

// The wheel this frame: each row at its place from the wheel's (-2..2 rows, wrapping round the four), the middle
// bright, the next ones dim, fading out beyond them.
void TitleTexts() {
    std::vector<ps2host::GameText> texts;
    const TitleMasks& m = TitleWordMasks();
    auto mix = [](u32 a, u32 b, float t) {
        u32 out = 0;
        for (int s = 0; s < 24; s += 8)
            out |= u32(float((a >> s) & 255u) + (float((b >> s) & 255u) - float((a >> s) & 255u)) * t + 0.5f) << s;
        return out;
    };
    for (int i = 0; i < 4; ++i) {
        float d = float(i) - g_title_pos;
        while (d >= 2.0f) d -= 4.0f;
        while (d < -2.0f) d += 4.0f;
        const float a = std::fabs(d);
        if (a >= 1.5f || !m.w[i]) continue;
        const float bright = std::max(0.0f, 1.0f - a), fade = std::clamp(1.5f - a, 0.0f, 0.5f) * 2.0f;
        texts.push_back(MaskLabel(m.word[i], m.w[i], 256.0f - float(m.w[i]) * 0.5f, kTitleMid + d * kTitleStep - 5.0f,
                                  mix(kTitleDim, kTitleBright, bright), fade));
    }
    g_title_drawn = !texts.empty();
    SetPlacedTexts(texts);
}

// The game's own wheel away while ours is drawn: its words (NEW GAME 40, LOAD GAME 44, OPTIONS 32 - half widths)
// and their glows (56 / 60 / 48), at x 256 between the rows above and below.
bool TitleSprite(u32 object) {
    if (!TitleActive() || !TitleReady()) return false;
    const float x = F32(Read32(object + 0x50u)), y = F32(Read32(object + 0x54u));
    if (std::fabs(x - 256.0f) > 0.5f || y < kTitleMid - 1.5f * kTitleStep || y > kTitleMid + 1.5f * kTitleStep)
        return false;
    const u32 buffer = Read32(object + (ps2host::GuestMemory().Read(object + 0x92u, 1) ? 0x94u : 0x98u));
    const float w = CameraOk(buffer) ? F32(Read32(buffer + 0x50u)) : 0.0f;
    bool word = false, glow = false;
    for (float want : {40.0f, 44.0f, 32.0f}) word = word || std::fabs(w - want) < 0.5f;
    for (float want : {56.0f, 60.0f, 48.0f}) glow = glow || std::fabs(w - want) < 0.5f;
    if (!word && !glow) return false;
    const u32 now = Read32(kFrameCounter);
    if (g_title_confirmed && now - g_title_confirmed < 600u) {
        // A row chosen: its flash as NEW GAME's (his look 2026-11-21) - NEW GAME's screen goes after 20 frames;
        // LOAD GAME / OPTIONS keep the glow and the word fading out for longer: everything away after those 20.
        static u32 glow_seen = 0;
        if (now - g_title_confirmed > 20u) return true;
        if (glow) {
            glow_seen = now;
            return false;
        }
        return now - glow_seen > 4u;
    }
    return true;
}

// The wheel's input (0x00414B68's own: the pad word 0x004AA4E0, 0x2008 confirm, 0x10 up, 0x40 down), ours first.
void TitleCursor(lifted::Context& c) {
    const u32 obj = u32(c.r[4]);
    // (Only the main menu's wheel of three: the difficulty's has more rows - and the disc's file as expected.)
    if (!TitleReady() || Read32(obj + 0x5C14u) != 3u) {
        F_00414B68(c);
        return;
    }
    if (!TitleActive()) {                           // the menu (again): from the game's own row
        g_title_row = int(Read32(obj + 0x5C18u)) % 3;
        g_title_pos = float(g_title_row);
        g_title_confirmed = 0;
    }
    if (g_title_confirmed) {                        // a row chosen: the game's own until the menu is left
        g_title_seen = Read32(kFrameCounter);
        F_00414B68(c);
        return;
    }
    g_title_seen = Read32(kFrameCounter);
    // The roll: a sixth of a row a frame towards the chosen one, the short way round.
    float gap = float(g_title_row) - g_title_pos;
    while (gap > 2.0f) gap -= 4.0f;
    while (gap < -2.0f) gap += 4.0f;
    const bool rolling = std::fabs(gap) > 0.001f;
    if (rolling) {
        g_title_pos += std::fabs(gap) <= 1.0f / 6.0f ? gap : (gap > 0.0f ? 1.0f : -1.0f) / 6.0f;
        if (g_title_pos < 0.0f) g_title_pos += 4.0f;
        if (g_title_pos >= 4.0f) g_title_pos -= 4.0f;
    }
    const u32 pad = u32(ps2host::GuestMemory().Read(kPadHeld, 2));
    const bool up = (pad & 0x50u) == 0x10u, down = (pad & 0x50u) == 0x40u, confirm = (pad & 0x2008u) != 0;
    static const bool log = std::getenv("ZOE_TITLE_LOG") != nullptr;      // (diagnosis: each frame's look)
    if (log)
        std::fprintf(stderr, "[title] frame %u pad %04X row %d pos %.2f game %u/%u\n", Read32(kFrameCounter), pad,
                     g_title_row, g_title_pos, Read32(obj + 0x5C10u), Read32(obj + 0x5C18u));
    c.r[2] = 0;
    if (Read32(obj + 0x5C10u) != Read32(obj + 0x5C18u)) {
        F_00414B68(c);                              // the game's own (hidden) roll on: its frames counted down
        c.r[2] = 0;
        TitleTexts();
        return;
    }
    if (!rolling && (up || down)) {
        g_title_row = (g_title_row + (down ? 1 : 3)) % 4;
        // The game's cursor follows ours the game's way - its target and its 6 frames (0x00414B68's own move;
        // writing the row itself made the menu leave its state); on EXIT GAME it stays where it is.
        if (g_title_row != kTitleExit && Read32(obj + 0x5C10u) != u32(g_title_row)) {
            Write32(obj + 0x5C18u, u32(g_title_row));
            Write32(obj + 0x5C1Cu, 6);
        }
        GameSound(c, 0x39);
    } else if (!rolling && confirm && g_title_row == kTitleExit) {
        GameSound(c, 0x3E);
        std::fprintf(stderr, "[title] EXIT GAME\n");
        ps2host::QuitGame();
    } else if (!rolling && confirm) {
        F_00414B68(c);                              // the game's own confirm on its row (ours): its v0 stands
        if (c.r[2]) {                               // chosen: the game's own flash of its word from here on
            g_title_confirmed = Read32(kFrameCounter);
            ps2host::SetGameTexts({});
            g_title_drawn = false;
            return;
        }
        TitleTexts();
        return;
    }
    c.r[2] = 0;                                     // (the sound's call left its own v0: no confirm for the menu)
    TitleTexts();
}

// Leaving the menu: our words away.
void TitleTick() {
    if (g_title_drawn && !TitleActive()) {
        ps2host::SetGameTexts({});
        g_title_drawn = false;
    }
}

// SELECT's EXIT TO MENU (the maintainer 2026-11-20: to the main menu, not the program started again): the game's
// own way back, GAME OVER's EXIT - 0x003E6188 makes the task (0x003E5FA0 / its frames 0x003E6008) that fades out,
// stops the scene, the sound and the stage, and starts the game over from its boot (ADA, the logo, the title).
// Asked from the window pump (the game held), done at the game's next frame: after 0x0044C7D0 (the main loop's
// per-frame call, 0x00460350), with the guest's registers kept round it.
// SELECT opens only in gameplay (the maintainer 2026-11-21): 3D on the screen with the HUD's sprites drawn in the
// last ten blanks, no movie - not the title, its menus, movies, loads, scenes without the HUD.
std::atomic<u64> g_last_hud_ns{0};     // a HUD sprite drawn (HudSprite)
bool SelectAllowed() {
    const u64 now = ps2host::LastVblankNs(), hud = g_last_hud_ns.load(std::memory_order_relaxed);
    const u64 movie = ps2host::LastMoviePictureNs();
    return Shows3d() && hud && now - hud <= 10u * 16683334u && (!movie || now - movie > 10u * 16683334u);
}

std::atomic<bool> g_exit_to_menu{false};
void ExitToMenu() { g_exit_to_menu.store(true); }
void FrameEnd(lifted::Context& c) {
    F_0044C7D0(c);
    DubSampleScene();                       // SUBTITLE TIMING's scene: the one read last
    if (g_japanese_subtitle_frame && Read32(kFrameCounter) - g_japanese_subtitle_frame > 1u) {
        ps2host::SetGameSubtitleTexts({});      // the Japanese subtitle's cue over
        g_japanese_subtitle_frame = 0;
    }
    TitleTick();                            // the title's EXIT GAME words away once its menu is left
    if (!g_exit_to_menu.exchange(false)) return;
    u64 saved[32], saved_hi[32];
    for (int i = 0; i < 32; ++i) saved[i] = c.r[i], saved_hi[i] = c.rh[i];
    F_003E6188(c);
    for (int i = 0; i < 32; ++i) c.r[i] = saved[i], c.rh[i] = saved_hi[i];
    std::fprintf(stderr, "[select] EXIT TO MENU: the game's own exit (GAME OVER's) started\n");
}

// BUTTON MAPPING in the game's look (the maintainer 2026-11-21: a port's menus look like the game's own): the
// engine's page (its rows, binding, capture) drawn as our pages are - the title, the rows a scrolling list of
// twelve (each input and its source; DEVICE, RESET TO DEFAULT first), the chosen one in its green band, a source
// bound twice in red, the hint.
void ControlsLook(bool open, const std::vector<ps2host::ControlsLine>& lines, int row) {
    if (!open) {
        ps2host::SetGameTexts({});
        return;
    }
    auto text = [](const std::string& s, float x, float y, u32 colour) {
        return ps2host::GameText{s, x / 512.0f, (y + 2.0f) / 448.0f, kLetters / 448.0f, colour, false, kStretch};
    };
    auto box = [](float x, float y, float w, float h, u32 colour, std::uint8_t cover) {
        ps2host::GameText g;
        g.mask.assign(16, cover);
        g.mask_w = g.mask_h = 4;
        g.x = x / 512.0f;
        g.y = y / 448.0f;
        g.width = w / 512.0f;
        g.height = h / 448.0f;
        g.colour = colour;
        return g;
    };
    std::vector<ps2host::GameText> texts;
    texts.push_back(box(0.0f, 0.0f, 512.0f, 448.0f, 0x000000u, g_select_settings ? 225 : 200));   // a dark ground
    // The list in a window of its own (as SELECT's), the OPTIONS screen's green lines - they ran through its rows,
    // his shot 2026-11-21 - covered by it; the OPTIONS screen's other pages keep them.
    texts.push_back(box(64.0f, 54.0f, 384.0f, 288.0f, 0x0A0D0Bu, 255));
    texts.push_back(box(64.0f, 54.0f, 384.0f, 1.0f, 0x2CC488u, 255));
    texts.push_back(box(64.0f, 341.0f, 384.0f, 1.0f, 0x2CC488u, 255));
    texts.push_back(box(64.0f, 54.0f, 1.0f, 288.0f, 0x2CC488u, 255));
    texts.push_back(box(447.0f, 54.0f, 1.0f, 288.0f, 0x2CC488u, 255));
    texts.push_back(text(g_select_settings ? "SETTINGS - BUTTON MAPPING" : "RECOMP OPTIONS - BUTTON MAPPING", 82.0f,
                         66.0f, kTitle));
    constexpr int kShown = 12;
    constexpr float kTop = 104.0f, kStep = 20.0f;
    const int n = int(lines.size());
    const int first = std::clamp(row - kShown / 2, 0, std::max(0, n - kShown));
    for (int i = first; i < std::min(n, first + kShown); ++i) {
        const ps2host::ControlsLine& l = lines[std::size_t(i)];
        const float y = kTop + kStep * float(i - first);
        const bool picked = i == row;
        if (picked) texts.push_back(box(72.0f, y - 3.0f, 368.0f, 16.0f, 0x2CC488u, 60));
        texts.push_back(text(l.label, 82.0f, y, picked ? 0x46F098u : kGrey));
        if (!l.value.empty())
            texts.push_back(text(l.value, 230.0f, y, l.capture ? 0x46F098u : l.doubled ? kRed : kWordGrey));
    }
    AddHints(&texts, false);
    SetPlacedTexts(texts);
}

// SELECT's SETTINGS (the maintainer 2026-11-20): our pages with the game held - the engine's input each window pump
// (PageInput without the game's sounds), drawn over a dark ground; false once they are closed (back on SELECT).
bool SelectSettingsStep(unsigned held, unsigned pressed) {
    if (!g_select_settings) {
        g_select_settings = true;
        OpenPage(kTopPage, 0);
    } else {
        PageInput(nullptr, held, pressed);
    }
    if (!PageOpen()) {
        g_select_settings = false;
        ps2host::SetGameTexts({});
        return false;
    }
    OptionsTexts();
    return true;
}

void OptionsFrame(lifted::Context& c) {
    const u32 obj = u32(c.r[4]);
    const u32 frame = Read32(kFrameCounter);
    if (obj != g_options_obj || frame - g_options_seen > 4) {        // entered anew
        g_recomp_row = false;
        PageClosed();
    }
    g_options_obj = obj;
    g_options_seen = frame;
    F_00420E88(c);
    OptionsTexts();
}

// Off the OPTIONS screen: the texts gone (called where the game draws, each frame).
void OptionsLabelTick() {
    if (g_options_seen && Read32(kFrameCounter) - g_options_seen > 2) {
        ps2host::SetGameTexts({});
        g_options_seen = 0;
        g_recomp_row = false;
        PageClosed();
    }
}

// The cursor over the four rows left (VIBRATION, SOUND, RECOMP OPTIONS, EXIT): the game moves it over its six,
// past CAPTION's and DEFAULT's (moved on again at once); RECOMP OPTIONS between SOUND and EXIT is our flag on EXIT.
void OptionsCursor(lifted::Context& c) {
    const u32 state = u32(c.r[4]) + kOptionsState;
    // The game builds its green frame again only when its row changes: on our pages its row goes between DEFAULT's
    // and EXIT's (the same plain frame) at each move - the place is ours anyway (OptionsFrameDraw) -, back on EXIT's
    // when the pages close.
    if (PageOpen()) {                               // our page: its input, the game's cursor still
        PageInput(&c, u32(ps2host::GuestMemory().Read(kPadHeld, 2)), u32(ps2host::GuestMemory().Read(kPadPressed, 2)));
        if (g_frame_moved) {
            Write32(state + kOptionsRow, Read32(state + kOptionsRow) == 4u ? kExitRow : 4u);
            Write32(state + kOptionsWait, 10);      // the frame moves while the move's wait runs down
        }
        g_frame_moved = false;
        if (Read32(state + kOptionsWait)) F_00422FF0(c);    // (only its wait counted down: no row move meanwhile)
        c.r[2] = 0;
        return;
    }
    if (g_frame_moved) {                            // back from a page: on RECOMP OPTIONS (EXIT's row, our flag)
        Write32(state + kOptionsRow, kExitRow);
        Write32(state + kOptionsWait, 10);
        g_frame_moved = false;
    }
    const u32 wait = Read32(state + kOptionsWait);
    const u32 held = u32(ps2host::GuestMemory().Read(kPadHeld, 2)) & 0xF0u;
    const u32 row = Read32(state + kOptionsRow);
    if (wait == 0 && row == u32(kExitRow) && ((g_recomp_row && held == kHeldDown) || (!g_recomp_row && held == kHeldUp))) {
        g_recomp_row = !g_recomp_row;               // RECOMP OPTIONS <-> EXIT: the game stays on EXIT's row
        Write32(state + kOptionsWait, 10);
        Write32(state + 0x9F8u, 1);                 // its frame built again
        GameSound(c, 0x39);
        c.r[2] = 0;
        return;
    }
    if (g_recomp_row && wait == 0 && held == kHeldUp) g_recomp_row = false;     // up from it: the game's move
    if (g_recomp_row) {                             // left / right: nothing on our line; the wait counted down
        if (held == kHeldLeft || held == kHeldRight) {
            c.r[2] = 0;
            return;
        }
        F_00422FF0(c);
        return;
    }
    F_00422FF0(c);
    // A hidden row reached (the game moves one row a press): on to the next shown one - down from VIBRATION SOUND,
    // down from SOUND RECOMP OPTIONS, up from SOUND VIBRATION. (Its move changes the row, the wait and plays the
    // sound only; the value column +0x9F0 is the screen's one toggle, not the row's.)
    const u32 now = Read32(state + kOptionsRow);
    if (now != row && (now == 1 || now == 2 || now == 4)) {
        if (now == 4 && held == kHeldDown) {
            Write32(state + kOptionsRow, kExitRow);
            g_recomp_row = true;
        } else if (now == 4) {
            Write32(state + kOptionsRow, kSoundRow);
        } else {
            Write32(state + kOptionsRow, held == kHeldDown ? kSoundRow : kVibrationRow);
        }
    }
}

void OptionsPress(lifted::Context& c) {
    if (PageOpen()) {
        c.r[2] = 0;
        return;
    }
    const u32 state = u32(c.r[4]) + kOptionsState;
    const u32 pressed = u32(ps2host::GuestMemory().Read(kPadPressed, 2)) & 0x7000u;
    const u32 row = Read32(state + kOptionsRow);
    if (pressed == kPressDefault && !g_recomp_row && (row == u32(kVibrationRow) || row == u32(kSoundRow))) {
        // Triangle: the row back to its default (vibration on, stereo), its value's column with it.
        GameSetter(c, row == u32(kVibrationRow) ? F_004663B0 : F_004664A0, true);
        Write32(state + 0x9F0u, 0);
        GameSound(c, 0x39);
        c.r[2] = 0;
        return;
    }
    if (!g_recomp_row) {
        F_00420FA0(c);
        return;
    }
    if (pressed == kPressConfirm && Read32(state + kOptionsWait) == 0) {    // confirm: our pages
        GameSound(c, 0x3A);
        OpenPage(kTopPage);
        c.r[2] = 0;
        return;
    }
    if (pressed == kPressBack) {                                    // back: as on any row
        g_recomp_row = false;
        F_00420FA0(c);
        return;
    }
    c.r[2] = 0;
}

// The green frame at the row shown: the drawn row's two sub-rows (four floats each) moved for the draw.
void OptionsFrameDraw(lifted::Context& c, void (*draw)(lifted::Context&)) {
    if (!g_options_obj || !CameraOk(g_options_obj)) {
        draw(c);
        return;
    }
    const u32 row = std::min<u32>(Read32(g_options_obj + kOptionsState + kOptionsRow), 5u);
    float to = kGameRowY[row];
    if (PageOpen()) to = kPageTop + kPageStep * float(g_page_row);
    else if (g_recomp_row) to = kRecompY;
    else if (row == u32(kSoundRow)) to = kSoundY;
    else if (row == u32(kExitRow)) to = kExitY;
    const float dy = to - kGameRowY[row];
    if (dy == 0.0f) {
        draw(c);
        return;
    }
    Changed changed;
    for (u32 i = 0; i < 2 * 4; ++i) {
        const u32 at = kFrameTable + (2 * row) * 16 + i * 4;
        changed.PutF(at, F32(Read32(at)) + dy);
    }
    draw(c);
    changed.Back();
}
void OptionsFrameA(lifted::Context& c) { OptionsFrameDraw(c, F_00423240); }
void OptionsFrameB(lifted::Context& c) { OptionsFrameDraw(c, F_004235A0); }

void HudWindow(lifted::Context& c) {
    OptionsLabelTick();
    const u32 window = u32(c.r[4]);
    HudLogOther("window", window, 0u);
    const float s = WideS();
    if (s >= 0.999f || !CameraOk(window) || !ShowsWide()) {
        F_0045ABE0(c);
        return;
    }
    const u16 flags = u16(ps2host::GuestMemory().Read(window + 2u, 2));
    bool own = (flags & 0x40u) != 0;
    bool identity = false;
    if (!own && ps2host::GuestMemory().Read(window, 1) == 3u) {
        identity = true;
        for (u32 i = 0; i < 16 && identity; ++i)
            identity = std::fabs(F32(Read32(window + 0x20u + i * 4u)) - ((i & 3u) == (i >> 2) ? 1.0f : 0.0f)) < 1e-4f;
    }
    if (!own && !identity) {
        F_0045ABE0(c);
        return;
    }
    // Its children: all inheriting? the clip rectangle, the widest sprite.
    u32 clip = 0;
    float widest = 0.0f;
    bool inherits = true;
    const u32 sentinel = window + 0xA0u;
    u32 child = Read32(window + 0x8Cu);
    for (u32 guard = 0; child != sentinel && inherits; ++guard, child = Read32(child + 0xCu)) {
        if (guard > 256u || !CameraOk(child)) {
            inherits = false;
            break;
        }
        const u16 f = u16(ps2host::GuestMemory().Read(child + 2u, 2));
        if (f & 2u) continue;                       // hidden: not drawn
        const u32 type = u32(ps2host::GuestMemory().Read(child, 1));
        if (type == 1u) {
            inherits = (f & 0x4u) && !(f & (0x400u | 0x80u | 0x20u));
            float lo = 0, hi = 0;
            if (SpriteExtent(child, &lo, &hi)) widest = std::max(widest, (hi - lo) * std::fabs(F32(Read32(child + 0x20u))));
            else if (identity) widest = 1e9f;       // a child that cannot be measured: as full-screen
        } else if (type == 4u) {
            const u16 sf = u16(ps2host::GuestMemory().Read(child + 0x22u, 2));
            inherits = (sf & 0x4u) && !(sf & 0x22u);
        } else if (type == 2u) {
            const u32 fn = Read32(child + 0xA0u);
            if (fn == kClipFn && CameraOk(Read32(child + 0xA4u))) clip = Read32(child + 0xA4u);
            else inherits = fn == kClipEndFn;
        } else {
            inherits = false;
        }
    }
    const float full = 0.9f * 512.0f;
    const bool full_screen = widest * std::fabs(F32(Read32(window + 0x20u))) >= full ||
                             (clip && F32(Read32(clip + 8u)) >= full);
    const float px = F32(Read32(window + 0x50u));
    if (!inherits || full_screen || (identity && clip) || !(px > -512.0f && px < 1024.0f)) {
        F_0045ABE0(c);
        return;
    }
    float b = 256.0f * (1.0f - s);
    if (clip) {
        const float rx = F32(Read32(clip)), rw = F32(Read32(clip + 8u));
        if (rw > 0.0f && rx > -512.0f && rx < 1024.0f) b = ZoneOffset(rx + 0.5f * rw, s);
        if (b != 0.0f && b != 512.0f * (1.0f - s)) b = (1.0f - s) * (rx + 0.5f * rw);   // between: about its centre
    }
    Changed changed;
    for (u32 r = 0; r < 4; ++r) {               // row r: x at +0, w at +0xC
        const u32 a = window + 0x20u + r * 0x10u;
        changed.PutF(a, s * F32(Read32(a)) + b * F32(Read32(a + 0xCu)));
    }
    if (clip) {
        changed.PutF(clip, s * F32(Read32(clip)) + b);
        changed.PutF(clip + 8u, s * F32(Read32(clip + 8u)));
    }
    if (identity) changed.Put(window, Read32(window) | 0x00400000u);    // flag 0x40 (+2's bit 6): its own matrix
    F_0045ABE0(c);
    changed.Back();
}

std::pair<u64, u64> HudLogRange() {
    static const std::pair<u64, u64> range = [] {
        unsigned long long a = 1, b = 0;
        if (const char* e = std::getenv("ZOE_HUD_LOG")) std::sscanf(e, "%llu-%llu", &a, &b);
        return std::make_pair(u64(a), u64(b));
    }();
    return range;
}
void HudSpriteDraw(lifted::Context& c);

// The OPTIONS screen's rows first (hidden or moved: OptionsSprite), then the sprite as below.
void HudSprite(lifted::Context& c) {
    g_last_hud_ns.store(ps2host::LastVblankNs(), std::memory_order_relaxed);
    OptionsLabelTick();
    float dy = 0;
    const u32 object = u32(c.r[4]);
    if (OptionsSprite(object, &dy)) return;                 // not drawn
    if (TitleSprite(object)) return;                        // the title's wheel: a word ours stands in for
    if (dy == 0.0f) {
        HudSpriteDraw(c);
        return;
    }
    Changed changed;
    changed.PutF(object + 0x54u, F32(Read32(object + 0x54u)) + dy);
    HudSpriteDraw(c);
    changed.Back();
}

void HudSpriteDraw(lifted::Context& c) {
    const u32 object = u32(c.r[4]);
    const auto range = HudLogRange();
    if (range.first <= range.second && CameraOk(object)) {
        const u64 n = Read32(kFrameCounter);
        // ZOE_HUD_LOG_EVERY=<k>: only every k-th game frame of the range (a played session's whole length).
        static const u64 every = [] {
            const char* e = std::getenv("ZOE_HUD_LOG_EVERY");
            return e ? std::max<u64>(1, std::strtoull(e, nullptr, 10)) : 1;
        }();
        static u64 said = ~0ull;    // the game's frame number beside the host's blank time, now and then
        if (n / 300 != said) {
            said = n / 300;
            std::fprintf(stderr, "[hud] game frame %llu at %.2f s\n", (unsigned long long)n,
                         double(ps2host::LastVblankNs()) / 1e9);
        }
        if (n >= range.first && n <= range.second && n % every == 0) {
            const u32 buffer = Read32(object + (ps2host::GuestMemory().Read(object + 0x92u, 1) ? 0x94u : 0x98u));
            std::fprintf(stderr,
                         "[hud] frame %llu obj %08X flags %04X pos %.1f,%.1f z %g ax %.3f,%.3f ay %.3f,%.3f verts %u "
                         "buf %08X w %g h %g\n",
                         (unsigned long long)n, unsigned(object), unsigned(ps2host::GuestMemory().Read(object + 2u, 2)),
                         F32(Read32(object + 0x50u)), F32(Read32(object + 0x54u)), F32(Read32(object + 0x58u)),
                         F32(Read32(object + 0x20u)), F32(Read32(object + 0x24u)), F32(Read32(object + 0x30u)),
                         F32(Read32(object + 0x34u)), unsigned(Read32(object + 0x8Cu)), unsigned(buffer),
                         CameraOk(buffer) ? F32(Read32(buffer + 0x50u)) : 0.0f,
                         CameraOk(buffer) ? F32(Read32(buffer + 0xC4u)) : 0.0f);
        }
    }
    // The wide picture's HUD: the same 512 columns hold a wider view, so each piece is narrowed by S = (4/3) /
    // aspect and kept at its edge - left of x 215 (its centre) by the left edge, from 297 by the right, between
    // by the screen's centre (ZOE-Recompiled's zones; all three the same S, only their anchor differs, so a row
    // of digits in one zone stays together). Pieces placed in GS coordinates (flag 0x20: a target box, from the
    // 3D view - already where the wide camera puts it) only narrowed about their place; a piece as wide as the
    // screen (an overlay, a fade) as it is: in the squeezed columns it covers the wide picture. The object's
    // place (+0x50) and its axes' x (+0x20, +0x30) changed for the packet's build alone, put back after.
    const float s = (4.0f / 3.0f) / ps2host::WideAspect();
    const u16 flags = u16(ps2host::GuestMemory().Read(object + 2u, 2));
    if (s >= 0.999f || !CameraOk(object) || (flags & 0x02u) || !ShowsWide()) {
        F_00459908(c);
        return;
    }
    const u32 buffer = Read32(object + (ps2host::GuestMemory().Read(object + 0x92u, 1) ? 0x94u : 0x98u));
    const float x = F32(Read32(object + 0x50u));
    float w = CameraOk(buffer) ? F32(Read32(buffer + 0x50u)) : 0.0f;
    if (!(w >= 0.0f && w <= 512.0f)) w = 0.0f;
    float to = x;
    if (flags & 0x20u) {
        to = x;                                     // GS coordinates: the wide camera placed it
    } else if (w >= 0.9f * 512.0f || (x == 256.0f && w >= 256.0f)) {
        F_00459908(c);                              // the whole width: as it is
        return;
    } else {
        // A piece starting at the left edge (x < 128) stays with it, one reaching the right edge (> 384) with that
        // - a bar's frame, its fill (whose length changes: by its centre it would jump zones as it shrinks) and
        // its name stay together (the boss bar: TEMPEST at 24, frame 86..494, fill from 88); others by their
        // centre's zone. Never stretched: a bar across the screen ends earlier in a wider picture.
        const float left = (flags & 0x8000u) ? x - w * 0.5f : x, right = left + w, centre = left + w * 0.5f;
        if (!Shows3d()) to = 256.0f + s * (x - 256.0f);         // a 2D screen: about the centre
        else if (left < 128.0f || (right <= 384.0f && centre < 215.0f)) to = s * x;
        else if (right > 384.0f || centre >= 297.0f) to = 512.0f - s * (512.0f - x);
        else to = 256.0f + s * (x - 256.0f);
    }
    const u32 saved[3] = {Read32(object + 0x50u), Read32(object + 0x20u), Read32(object + 0x30u)};
    const u32 wrote[3] = {Bits(to), Bits(F32(saved[1]) * s), Bits(F32(saved[2]) * s)};
    Write32(object + 0x50u, wrote[0]);
    Write32(object + 0x20u, wrote[1]);
    Write32(object + 0x30u, wrote[2]);
    F_00459908(c);
    const u32 at[3] = {0x50u, 0x20u, 0x30u};
    for (int i = 0; i < 3; ++i)
        if (Read32(object + at[i]) == wrote[i]) Write32(object + at[i], saved[i]);
}

// The game's language index (0x004DFCA4, beside its flags at 0x004DFCA0 - the 12 bytes a save's configuration
// copies back, 0x00466E38): 1 English, 2 French, 3 German, 4 Italian, as the PAL version maps the console's
// language into it (SLES 0x00466C98, 2026-11-13). The US program sets it to English at start, asking nothing
// (0x00466388) - yet the US disc's voiced scenes carry all four languages' subtitles (docs/languages_research.md).
// The "language" setting (en / fr / de / it) chooses; unset, English as before. Spanish (es, our own subtitles):
// the game in English, its English subtitle channel carrying the Spanish lines (dub.cpp).
u32 GameLanguage() {
    const std::string value = ps2host::Setting("language");
    if (value == "fr") return 2;
    if (value == "de") return 3;
    if (value == "it") return 4;
    return 1;
}

// 0x00466388 as it is - flags 0xF, the language, then 0x00466F18(0xFF000006) - with the language chosen.
void LanguageAtStart(lifted::Context& c) {
    ps2host::GuestMemory().Write(0x004DFCA0u, 0xFu, 4);
    ps2host::GuestMemory().Write(0x004DFCA4u, GameLanguage(), 4);
    DescriptionsInSpanish();            // (SPANISH: the executable's descriptions, dub.cpp)
    c.r[4] = 0xFFFFFFFFFF000006ull;     // lui $a0, 0xff00; ori $a0, $a0, 6 (sign-extended)
    F_00466F18(c);
}

// 0x00466E38(source): copies a save's 12 configuration bytes back to 0x004DFCA0 (the language with them - a US
// save always says English), then tells the sound driver the saved output mode (0x00466F18, which does not read
// the language). As it is, then the chosen language again: the setting wins over the save.
void ConfigurationFromSave(lifted::Context& c) {
    F_00466E38(c);
    ps2host::GuestMemory().Write(0x004DFCA4u, GameLanguage(), 4);
}

// 0x00466FB8: the address of IOP_MAIN's status block (its word at +4: bit 31 set while a command to the sound
// driver waits for its answer). 24 loops in the executable call it and branch back until bit 31 clears - the EE
// spinning for the IOP (scratchpad pollscan.py). The IOP ran only at interrupts meanwhile and answered a blank
// late: ZOE US's 66 ms holds every ~7 s in fights started in one (0x004375E8 sends three commands and polls;
// the heavy-blank sampler, toolkit 532935a, had 77 % of that blank there, the IOP given 17-40 k instructions
// instead of ~90 k; M4 2026-11-14). As it is, then - while the answer is still out - an IOP slice, its replies
// delivered (RunIopBetweenSteps; real time only), as the console's IOP runs beside the spinning EE.
void IopStatusPoll(lifted::Context& c) {
    F_00466FB8(c);
    if (ps2host::GuestMemory().Read(u32(c.r[2]) + 4u, 4) & 0x80000000u) ps2host::RunIopBetweenSteps();
}

}  // namespace

void RegisterDub();     // dub.cpp: the Japanese voices from the player's Japanese disc (M9)
std::string DubSceneLine();         // dub.cpp: SUBTITLE TIMING (the F9 debug panel)
void DubSceneStep(int step);

void RegisterGameOverrides() {
    RegisterDub();
    // The subtitle language (see GameLanguage): an experiment of the language research (M9 groundwork,
    // 2026-11-13) - the game's own variable, set where the game sets it.
    ps2host::Override(0x00466388u, LanguageAtStart, "language index from the 'language' setting (US: English fixed)");
    ps2host::Override(0x00466E38u, ConfigurationFromSave, "a loaded save's configuration, the language setting kept");
    // The PC settings' LANGUAGE line (M7, the ports' rule): the US disc's subtitle languages (docs/languages_research.md).
    // SPANISH and JAPANESE: ours, from the subtitle pack (a download of its own, dub.cpp) - its Noto Sans JP too.
    if (DubSubtitlePack()) {
        ps2host::SetLanguages({"en", "fr", "de", "it", "es", "ja"});   // (es / ja: the game in English, dub.cpp)
        const std::vector<unsigned char>& font = DubSubtitleFont();
        if (!font.empty()) ps2host::SetGameTextFallbackFont(font.data(), font.size());
    } else {
        ps2host::SetLanguages({"en", "fr", "de", "it"});
    }
    // DUB under LANGUAGE (M9, the maintainer 2026-11-20): only with the player's Japanese disc set in the launcher
    // (its second line, "disc_ja"); dub.cpp serves its voices at the next start.
    // The maintainer 2026-11-21: the Japanese voices need the subtitle pack too (its downloads show the demand).
    ps2host::SetDubs({"en", "ja"}, [] { return !ps2host::Setting("disc_ja").empty() && DubSubtitlePack(); });

    // ASPECT RATIO 16:9 / 16:10 / 21:9 and FIELD OF VIEW (M7 step 5): the cameras' projection (CameraMatrix,
    // CameraView), 3D play wide and 4:3 screens between bars (ShowsWide).
    ps2host::Override(0x00450C40u, CameraMatrix, "a camera's matrices built for ASPECT RATIO / FIELD OF VIEW");
    ps2host::Override(0x00450F58u, CameraView, "a camera's matrices built again when the setting changed");
    ps2host::Override(0x004597F0u, Object3d, "3D drawn: the picture is the wide one");
    ps2host::SetWidescreen(ShowsWide);
    // SELECT: EXIT GAME / EXIT TO MENU (issue 3; ZOE leaves Select unused - his FAQ and a scripted fight with and
    // without it, the same pictures). The internal "select" setting "stock" gives it back to the game.
    ps2host::EnableSelectMenu(true);
    ps2host::SetSelectMenuLook(SelectMenuLook);
    ps2host::SetSelectSettings(SelectSettingsStep);
    ps2host::SetControlsLook(ControlsLook);
    ps2host::SetExitToMenu(ExitToMenu);
    ps2host::SetSelectAllowed(SelectAllowed);
    // The default layout (the maintainer 2026-11-21): Start and Select trade places - SELECT's menu on the Start
    // button (and Enter), the game's pause on Select (and Backspace); BUTTON MAPPING changes it back.
    ps2host::bind::SwapDefaults(0, 3);
    ps2host::AddDebugLine("Subtitle timing", DubSceneLine, DubSceneStep);    // F9 (internal): the scene playing
    ps2host::Override(0x00414B68u, TitleCursor, "the title's main menu: EXIT GAME as a fourth row of its wheel");
    ps2host::Override(0x0044C7D0u, FrameEnd, "SELECT's EXIT TO MENU at the frame's end: GAME OVER's own exit");
    ps2host::Override(0x00459908u, HudSprite, "a HUD sprite's draw: placed for ASPECT RATIO (its edge or centre)");
    ps2host::Override(0x0045B668u, HudText, "a text object's draw (ASPECT RATIO, logged)");
    ps2host::Override(0x0045ABE0u, HudWindow, "a window container's draw (ASPECT RATIO, logged)");
    ps2host::Override(0x00282A20u, MenuLabel, "a menu label made (traced: ZOE_MENU_TRACE)");
    // The title OPTIONS screen: VIBRATION, SOUND, RECOMP OPTIONS (our pages), EXIT (M7 step 7, M9): see the
    // OPTIONS section above.
    ps2host::Override(0x00420E88u, OptionsFrame, "the OPTIONS screen each frame: its rows' texts, our pages");
    ps2host::Override(0x00422FF0u, OptionsCursor, "the OPTIONS cursor: four rows (CAPTION, DEFAULT passed), our pages");
    ps2host::Override(0x00420FA0u, OptionsPress, "the OPTIONS confirm: RECOMP OPTIONS opens our pages; triangle: default");
    ps2host::Override(0x00423240u, OptionsFrameA, "the OPTIONS frame drawn at the row shown");
    ps2host::Override(0x004235A0u, OptionsFrameB, "the OPTIONS frame drawn at the row shown");

    // ZOE's own IOP modules (Konami's, from cdrom0:\MODULES) run in the IOP interpreter for bring-up (tier E):
    // their protocols are the game's. Same set as the PAL port (games/zoe, M1 2026-11-06): start-up polls SIF
    // software register 8 for bit 1, which one of these sets once it runs. Shipped path (design 7d): these IRX
    // get translated ahead of time instead.
    ps2host::iop::RunModule("JSIFMAN.IRX");
    ps2host::iop::RunModule("CDBIOS.IRX");
    ps2host::iop::RunModule("NOUSBFS.IRX");
    ps2host::iop::RunModule("IOP_MAIN.IRX");

    // VU1 fast mode by default (M3, 2026-11-12): the scripted run port/runs/hangar.args (boot -> movies ->
    // cutscenes -> hangar, 33600 frames, --gpu) draws all 168 pictures the same as the exact VU
    // (regress.py --vu-fast), VU1 4.4% -> 3.6% of the run. --vu-exact turns it off.
    ps2host::SetVuFastDefault(true);

    // Idle loop (M3, 2026-11-12): the lowest-priority thread's endless stream poll, as in the PAL port
    // (games/zoe/port/overrides.cpp, 0x00435C78 there): `for (;;) F_00436818(stream)`, loop head 0x00435720.
    ps2host::AddIdleLoop(0x00435720u);

    // IOP waits (M4, 2026-11-14): see IopStatusPoll.
    ps2host::Override(0x00466FB8u, IopStatusPoll, "IOP_MAIN's status block: the IOP runs while the EE polls it");

    // The pipelines the game draws with, made while it starts (pipeline_keys.inc; 2026-11-18: a cold shader cache
    // took 8.3 s on one worker).
    ps2host::SetGpuPipelineHints(kPipelineKeys, sizeof kPipelineKeys / sizeof kPipelineKeys[0]);
}
