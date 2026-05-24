// Silent Woods — first-person horror with a microphone "don't scream" mechanic.
// Engine: raylib (3D, input, audio playback) + miniaudio (mic capture).
//
// Build:  ./build.sh   (downloads miniaudio.h, then cmake/make)
// Run:    ./build/silent_woods
//
// Controls:
//   WASD       — walk
//   Mouse      — look around
//   Shift      — sprint (uses more breath -> more mic noise risk)
//   Esc        — quit
//
// Gameplay:
//   You spawn next to an abandoned house in a dark forest.
//   Survive 15 minutes. Any loud sound from your microphone draws "it" closer.
//   Stay silent. Walk slowly. Don't scream.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// miniaudio on Windows pulls in <windows.h>, which defines several names
// that collide with raylib's API. Strip them so raylib's versions win.
#ifdef _WIN32
    #undef CloseWindow
    #undef ShowCursor
    #undef LoadImage
    #undef DrawText
    #undef DrawTextEx
    #undef PlaySound
    #undef Rectangle
#endif

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

// -------------------- microphone capture --------------------
static std::atomic<float> g_micLevel{0.0f};
static ma_device         g_micDevice;
static bool              g_micInited = false;

static void MicCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
    (void)device; (void)output;
    const float* in = (const float*)input;
    if (!in) return;
    double sum = 0.0;
    for (ma_uint32 i = 0; i < frameCount; i++) {
        float v = in[i];
        sum += v * v;
    }
    float rms = (float)std::sqrt(sum / (double)frameCount);
    float scaled = rms * 4.0f;
    if (scaled > 1.0f) scaled = 1.0f;
    float prev = g_micLevel.load();
    g_micLevel.store(prev * 0.6f + scaled * 0.4f);
}

static bool InitMic() {
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format   = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate       = 44100;
    cfg.dataCallback     = MicCallback;
    if (ma_device_init(NULL, &cfg, &g_micDevice) != MA_SUCCESS) return false;
    if (ma_device_start(&g_micDevice) != MA_SUCCESS) {
        ma_device_uninit(&g_micDevice);
        return false;
    }
    g_micInited = true;
    return true;
}

static void ShutdownMic() {
    if (g_micInited) { ma_device_uninit(&g_micDevice); g_micInited = false; }
}

// -------------------- procedural audio --------------------
static Sound MakeWave(int sampleRate, int samples, float (*fn)(int i, int total, int sr, void* ctx), void* ctx) {
    Wave w = {0};
    w.frameCount = (unsigned int)samples;
    w.sampleRate = (unsigned int)sampleRate;
    w.sampleSize = 16;
    w.channels   = 1;
    short* data = (short*)std::malloc(samples * sizeof(short));
    for (int i = 0; i < samples; i++) {
        float v = fn(i, samples, sampleRate, ctx);
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        data[i] = (short)(v * 32000.0f);
    }
    w.data = data;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

static float SampWhisper(int i, int total, int sr, void* ctx) {
    static float prev = 0.0f;
    float env = std::sin((float)i / total * PI);
    float r = ((float)std::rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    prev = prev * 0.88f + r * 0.12f;
    return prev * env * 0.7f;
}

static float SampHeart(int i, int total, int sr, void* ctx) {
    float t = (float)i / sr;
    auto thump = [&](float at) {
        float dt = t - at;
        if (dt < 0 || dt > 0.18f) return 0.0f;
        float env = std::exp(-dt * 14.0f);
        return std::sin(2.0f * PI * 55.0f * dt) * env;
    };
    return (thump(0.05f) + thump(0.30f) * 0.7f) * 0.9f;
}

static float SampBranch(int i, int total, int sr, void* ctx) {
    float t = (float)i / sr;
    if (t > 0.18f) return 0.0f;
    float env = std::exp(-t * 35.0f);
    float r = ((float)std::rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    float tone = std::sin(2.0f * PI * 140.0f * t);
    return (r * 0.6f + tone * 0.4f) * env;
}

static float SampWind(int i, int total, int sr, void* ctx) {
    static float p = 0.0f;
    float env = std::sin((float)i / total * PI);
    float r = ((float)std::rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    p = p * 0.96f + r * 0.04f;
    return p * env * 0.5f;
}

static float SampScreech(int i, int total, int sr, void* ctx) {
    float t = (float)i / sr;
    float dur = (float)total / sr;
    float env = std::sin((float)i / total * PI);
    float f1 = 180.0f + t * 1200.0f;
    float f2 = 250.0f + t * 800.0f;
    float v  = std::sin(2.0f * PI * f1 * t) +
               std::sin(2.0f * PI * f2 * t) * 0.7f +
               (((float)std::rand() / (float)RAND_MAX) * 2.0f - 1.0f) * 0.5f;
    return v * env * 0.4f;
}

static float SampOwl(int i, int total, int sr, void* ctx) {
    float t = (float)i / sr;
    float env = std::sin((float)i / total * PI);
    float f  = 260.0f + std::sin(t * 6.0f) * 25.0f;
    return std::sin(2.0f * PI * f * t) * env * 0.5f;
}

// -------------------- forest / world --------------------
struct Tree {
    Vector3 pos;
    float   scale;
    float   sway;
    Color   trunk;
    Color   leaves;
};

static void DrawTree(const Tree& t, float time) {
    float s = t.scale;
    float swayX = std::sin(time * 0.7f + t.sway) * 0.04f * s;
    float trunkH = 3.5f * s;
    Vector3 trunkBase = { t.pos.x, 0.0f, t.pos.z };
    Vector3 trunkTop  = { t.pos.x + swayX, trunkH, t.pos.z + swayX * 0.5f };
    DrawCylinderEx(trunkBase, trunkTop, 0.32f * s, 0.22f * s, 8, t.trunk);

    Vector3 canopy = { trunkTop.x, trunkTop.y + 0.6f * s, trunkTop.z };
    DrawSphereEx(canopy, 1.5f * s, 8, 8, t.leaves);
    DrawSphereEx({canopy.x + 0.4f*s, canopy.y - 0.2f*s, canopy.z + 0.3f*s}, 1.0f * s, 6, 6, t.leaves);
    DrawSphereEx({canopy.x - 0.4f*s, canopy.y - 0.3f*s, canopy.z - 0.2f*s}, 1.1f * s, 6, 6, t.leaves);
}

static void DrawHouse(Vector3 pos) {
    // walls
    Color wall  = {38, 28, 20, 255};
    Color trim  = {18, 12, 8, 255};
    Color roof  = {22, 14, 10, 255};
    Color glow  = {180, 110, 40, 255};

    DrawCube({pos.x, pos.y + 1.6f, pos.z}, 6.0f, 3.2f, 6.0f, wall);
    DrawCubeWires({pos.x, pos.y + 1.6f, pos.z}, 6.0f, 3.2f, 6.0f, trim);
    // roof — two stacked tapering cubes for a chunky pitched look
    DrawCube({pos.x, pos.y + 3.6f, pos.z}, 6.6f, 0.6f, 6.6f, roof);
    DrawCube({pos.x, pos.y + 4.2f, pos.z}, 5.2f, 0.6f, 5.2f, roof);
    DrawCube({pos.x, pos.y + 4.7f, pos.z}, 3.6f, 0.4f, 3.6f, roof);
    // door
    DrawCube({pos.x, pos.y + 1.0f, pos.z + 3.05f}, 1.1f, 2.0f, 0.1f, {10, 6, 3, 255});
    DrawCube({pos.x + 0.4f, pos.y + 1.0f, pos.z + 3.10f}, 0.08f, 0.08f, 0.02f, {200,180,120,255});
    // windows
    DrawCube({pos.x - 1.8f, pos.y + 2.0f, pos.z + 3.05f}, 0.9f, 0.9f, 0.05f, glow);
    DrawCube({pos.x + 1.8f, pos.y + 2.0f, pos.z + 3.05f}, 0.9f, 0.9f, 0.05f, glow);
    DrawCube({pos.x - 1.8f, pos.y + 2.0f, pos.z - 3.05f}, 0.9f, 0.9f, 0.05f, {80,50,20,255});
    DrawCube({pos.x + 1.8f, pos.y + 2.0f, pos.z - 3.05f}, 0.9f, 0.9f, 0.05f, {80,50,20,255});
    // chimney
    DrawCube({pos.x + 2.0f, pos.y + 4.8f, pos.z + 1.0f}, 0.6f, 1.6f, 0.6f, {22, 18, 16, 255});
}

static void DrawStalker(Vector3 p, float fearProximity /*0..1, 1 = right on top of you*/) {
    // Tall thin black silhouette with red eyes when very close.
    Color body = {0, 0, 0, 255};
    float h = 2.6f;
    DrawCylinderEx({p.x, 0.0f, p.z}, {p.x, h - 0.4f, p.z}, 0.22f, 0.28f, 6, body);
    DrawSphereEx({p.x, h - 0.1f, p.z}, 0.28f, 8, 8, body);
    // long arms
    DrawCylinderEx({p.x - 0.25f, h - 0.5f, p.z}, {p.x - 0.35f, 0.5f, p.z + 0.1f}, 0.06f, 0.08f, 6, body);
    DrawCylinderEx({p.x + 0.25f, h - 0.5f, p.z}, {p.x + 0.35f, 0.5f, p.z + 0.1f}, 0.06f, 0.08f, 6, body);
    if (fearProximity > 0.4f) {
        unsigned char a = (unsigned char)(255.0f * (fearProximity - 0.4f) / 0.6f);
        Color eye = {220, 30, 30, a};
        DrawSphereEx({p.x - 0.08f, h - 0.05f, p.z + 0.22f}, 0.035f, 6, 6, eye);
        DrawSphereEx({p.x + 0.08f, h - 0.05f, p.z + 0.22f}, 0.035f, 6, 6, eye);
    }
}

// -------------------- collision helpers --------------------
static bool CollidesWithWorld(Vector3 p, const std::vector<Tree>& trees, Vector3 housePos) {
    // house aabb (slightly inflated)
    if (p.x > housePos.x - 3.3f && p.x < housePos.x + 3.3f &&
        p.z > housePos.z - 3.3f && p.z < housePos.z + 3.3f) {
        return true;
    }
    // trees
    for (const auto& t : trees) {
        float dx = p.x - t.pos.x;
        float dz = p.z - t.pos.z;
        float r  = 0.45f + 0.22f * t.scale;
        if (dx*dx + dz*dz < r*r) return true;
    }
    // world bounds
    if (std::fabs(p.x) > 78.0f || std::fabs(p.z) > 78.0f) return true;
    return false;
}

// -------------------- main --------------------
int main(void) {
    const int SCR_W = 1280;
    const int SCR_H = 720;
    InitWindow(SCR_W, SCR_H, "Silent Woods");
    SetExitKey(0);
    InitAudioDevice();
    SetTargetFPS(60);

    bool micOK = InitMic();

    // Build forest
    std::mt19937 rng(20260524u);
    std::uniform_real_distribution<float> distXZ(-72.0f, 72.0f);
    std::uniform_real_distribution<float> distScale(0.7f, 1.7f);
    std::uniform_real_distribution<float> distSway(0.0f, 6.28f);
    std::uniform_int_distribution<int>    distShade(-6, 6);

    Vector3 housePos    = {0, 0, 0};
    Vector3 spawnPos    = {0, 1.7f, 8.0f};

    std::vector<Tree> trees;
    int attempts = 0;
    while ((int)trees.size() < 320 && attempts < 6000) {
        attempts++;
        Tree t;
        t.pos.x = distXZ(rng);
        t.pos.z = distXZ(rng);
        t.pos.y = 0.0f;
        t.scale = distScale(rng);
        t.sway  = distSway(rng);
        int sh = distShade(rng);
        t.trunk  = {(unsigned char)(28 + sh), (unsigned char)(20 + sh/2), (unsigned char)(14 + sh/3), 255};
        t.leaves = {(unsigned char)(14 + sh), (unsigned char)(24 + sh), (unsigned char)(16 + sh/2), 255};
        // keep clear around house
        if (std::fabs(t.pos.x) < 6.0f && std::fabs(t.pos.z) < 6.0f) continue;
        // keep clear around spawn
        float dx = t.pos.x - spawnPos.x;
        float dz = t.pos.z - spawnPos.z;
        if (dx*dx + dz*dz < 9.0f) continue;
        trees.push_back(t);
    }

    // Sounds
    Sound sndWhisper = MakeWave(44100, 44100 * 2,            SampWhisper, nullptr);
    Sound sndHeart   = MakeWave(44100, (int)(44100 * 0.6f),  SampHeart,   nullptr);
    Sound sndBranch  = MakeWave(44100, (int)(44100 * 0.25f), SampBranch,  nullptr);
    Sound sndWind    = MakeWave(44100, 44100 * 3,            SampWind,    nullptr);
    Sound sndScreech = MakeWave(44100, (int)(44100 * 1.2f),  SampScreech, nullptr);
    Sound sndOwl     = MakeWave(44100, (int)(44100 * 0.9f),  SampOwl,     nullptr);
    SetSoundVolume(sndWind, 0.35f);
    SetSoundVolume(sndWhisper, 0.6f);
    SetSoundVolume(sndHeart, 0.7f);
    SetSoundVolume(sndBranch, 0.7f);
    SetSoundVolume(sndOwl, 0.55f);

    // Camera (manual yaw/pitch — raylib's built-in first-person mode doesn't handle our collision)
    Camera3D cam = {0};
    cam.position   = spawnPos;
    cam.target     = {spawnPos.x, spawnPos.y, spawnPos.z - 1.0f};
    cam.up         = {0, 1, 0};
    cam.fovy       = 72.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    float yaw   = -PI;        // looking toward house (-Z)
    float pitch = 0.0f;
    const float mouseSens = 0.0025f;

    // Stalker
    Vector3 stalkerPos    = {40.0f, 0.0f, 40.0f};
    float   stalkerTarget = 40.0f;  // desired distance from player

    // State
    enum class State { Menu, Playing, Lost, Won };
    State  state = State::Menu;
    float  timeLeft   = 15.0f * 60.0f;
    float  bobPhase   = 0.0f;
    float  eventTimer = 6.0f;
    float  windTimer  = 0.0f;
    float  jumpscare  = 0.0f;  // seconds remaining of flash
    float  vignettePulse = 0.0f;
    bool   headphonesShown = true;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f;
        float now = (float)GetTime();

        // ---------------- menu ----------------
        if (state == State::Menu) {
            ShowCursor();
            EnableCursor();
            if ((micOK && (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)))) {
                state = State::Playing;
                DisableCursor();
                timeLeft = 15.0f * 60.0f;
                stalkerTarget = 40.0f;
                stalkerPos = {40.0f, 0.0f, 40.0f};
                cam.position = spawnPos;
                yaw = -PI; pitch = 0.0f;
                jumpscare = 0.0f;
            }
        }

        if (state == State::Playing) {
            // ---------------- look ----------------
            Vector2 md = GetMouseDelta();
            yaw   -= md.x * mouseSens;
            pitch -= md.y * mouseSens;
            const float pitchMax = 1.3f;
            if (pitch >  pitchMax) pitch =  pitchMax;
            if (pitch < -pitchMax) pitch = -pitchMax;

            Vector3 fwd = {
                std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::cos(yaw)
            };
            Vector3 fwdFlat = { fwd.x, 0.0f, fwd.z };
            float flatLen = std::sqrt(fwdFlat.x*fwdFlat.x + fwdFlat.z*fwdFlat.z);
            if (flatLen > 0.0001f) { fwdFlat.x /= flatLen; fwdFlat.z /= flatLen; }
            Vector3 rightFlat = { fwdFlat.z, 0.0f, -fwdFlat.x };

            // ---------------- move ----------------
            bool sprinting = IsKeyDown(KEY_LEFT_SHIFT);
            float speed = sprinting ? 4.6f : 2.4f;
            Vector3 moveDir = {0, 0, 0};
            bool moving = false;
            if (IsKeyDown(KEY_W)) { moveDir.x += fwdFlat.x;   moveDir.z += fwdFlat.z;   moving = true; }
            if (IsKeyDown(KEY_S)) { moveDir.x -= fwdFlat.x;   moveDir.z -= fwdFlat.z;   moving = true; }
            if (IsKeyDown(KEY_D)) { moveDir.x += rightFlat.x; moveDir.z += rightFlat.z; moving = true; }
            if (IsKeyDown(KEY_A)) { moveDir.x -= rightFlat.x; moveDir.z -= rightFlat.z; moving = true; }
            float mlen = std::sqrt(moveDir.x*moveDir.x + moveDir.z*moveDir.z);
            if (mlen > 0.0001f) {
                moveDir.x /= mlen; moveDir.z /= mlen;
                Vector3 step = { moveDir.x * speed * dt, 0.0f, moveDir.z * speed * dt };
                // try X then Z independently for nicer sliding
                Vector3 attempt = cam.position;
                attempt.x += step.x;
                if (!CollidesWithWorld(attempt, trees, housePos)) cam.position.x = attempt.x;
                attempt = cam.position;
                attempt.z += step.z;
                if (!CollidesWithWorld(attempt, trees, housePos)) cam.position.z = attempt.z;
            }

            // head bob
            if (moving) bobPhase += dt * (sprinting ? 9.5f : 6.0f);
            float bob = std::sin(bobPhase) * 0.05f * (moving ? 1.0f : 0.0f);
            cam.position.y = 1.7f + bob;

            // sprinting bumps "breath" — auto-adds to mic level via fake offset
            float micRaw = g_micLevel.load();
            float micEff = micRaw + (sprinting && moving ? 0.04f : 0.0f);
            if (micEff > 1.0f) micEff = 1.0f;

            cam.target = { cam.position.x + fwd.x,
                           cam.position.y + fwd.y,
                           cam.position.z + fwd.z };

            // ---------------- stalker logic ----------------
            const float NOISE_THRESHOLD = 0.18f;
            const float PANIC_THRESHOLD = 0.42f;
            if (micEff > NOISE_THRESHOLD) {
                float over = micEff - NOISE_THRESHOLD;
                stalkerTarget -= dt * (1.0f + over * 18.0f);
            } else {
                stalkerTarget += dt * 0.35f;
            }
            if (stalkerTarget > 40.0f) stalkerTarget = 40.0f;
            if (stalkerTarget < 0.0f)  stalkerTarget = 0.0f;

            // move stalker so its distance from player approaches stalkerTarget
            Vector3 toPlayer = { cam.position.x - stalkerPos.x, 0.0f, cam.position.z - stalkerPos.z };
            float d = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
            if (d > 0.001f) {
                Vector3 dir = { toPlayer.x / d, 0.0f, toPlayer.z / d };
                float gap   = d - stalkerTarget;
                float pace  = (gap > 0 ? 1.5f : 0.6f);
                stalkerPos.x += dir.x * gap * pace * dt;
                stalkerPos.z += dir.z * gap * pace * dt;
            }

            float fearProximity = 1.0f - (stalkerTarget / 40.0f);
            vignettePulse = fearProximity;

            if (stalkerTarget < 1.8f && jumpscare <= 0.0f) {
                jumpscare = 1.2f;
                PlaySound(sndScreech);
            }

            if (jumpscare > 0.0f) {
                jumpscare -= dt;
                if (jumpscare <= 0.0f) state = State::Lost;
            }

            // ---------------- ambient events ----------------
            eventTimer -= dt;
            if (eventTimer <= 0.0f) {
                int r = std::rand() % 100;
                if      (r < 30) { PlaySound(sndWhisper); }
                else if (r < 55) { PlaySound(sndBranch); stalkerTarget -= 0.4f; }
                else if (r < 75) { PlaySound(sndHeart);  }
                else if (r < 90) { PlaySound(sndOwl);    }
                else             { PlaySound(sndWind);   }
                eventTimer = 6.0f + (std::rand() % 100) / 10.0f;  // 6 .. 16 s
            }
            windTimer -= dt;
            if (windTimer <= 0.0f) {
                PlaySound(sndWind);
                windTimer = 18.0f + (std::rand() % 12);
            }

            // ---------------- timer ----------------
            timeLeft -= dt;
            if (timeLeft <= 0.0f) state = State::Won;
        }

        // ---------------- draw ----------------
        BeginDrawing();
        ClearBackground({4, 4, 8, 255});

        BeginMode3D(cam);

        // Ground — large dark plane
        DrawPlane({0, 0, 0}, {300, 300}, {10, 9, 7, 255});

        // House
        DrawHouse(housePos);

        // Trees: simple distance cull
        for (const auto& t : trees) {
            float dx = t.pos.x - cam.position.x;
            float dz = t.pos.z - cam.position.z;
            if (dx*dx + dz*dz > 70.0f * 70.0f) continue;
            DrawTree(t, now);
        }

        // Moon — distant glowing sphere
        DrawSphereEx({40.0f, 35.0f, -60.0f}, 3.0f, 12, 12, {220, 220, 210, 255});

        // Stalker
        if (state == State::Playing || state == State::Lost) {
            float fp = (state == State::Playing) ? (1.0f - stalkerTarget / 40.0f) : 1.0f;
            float dx = stalkerPos.x - cam.position.x;
            float dz = stalkerPos.z - cam.position.z;
            if (dx*dx + dz*dz < 65.0f * 65.0f) DrawStalker(stalkerPos, fp);
        }

        EndMode3D();

        // ---------------- fog overlay (fake) ----------------
        // Darken edges of screen + extra darkness at horizon
        DrawRectangle(0, SCR_H * 2 / 3, SCR_W, SCR_H / 3, {0, 0, 0, 80});
        // vignette: simple corner darkening
        for (int i = 0; i < 6; i++) {
            int inset = i * 30;
            DrawRectangleLinesEx({(float)inset, (float)inset,
                                  (float)(SCR_W - 2*inset), (float)(SCR_H - 2*inset)},
                                 30.0f, {0, 0, 0, (unsigned char)(40 - i * 5)});
        }

        // ---------------- HUD ----------------
        if (state == State::Playing) {
            float micRaw = g_micLevel.load();
            DrawText("MIC", 24, 20, 18, {180, 180, 180, 200});
            DrawRectangle(24, 46, 220, 14, {30, 30, 30, 200});
            Color barCol = (micRaw > 0.42f) ? Color{220, 40, 40, 255}
                          : (micRaw > 0.18f ? Color{220, 140, 30, 255}
                                            : Color{60, 160, 60, 255});
            DrawRectangle(24, 46, (int)(220 * micRaw), 14, barCol);
            DrawRectangleLines(24, 46, 220, 14, {120, 120, 120, 200});
            if (!micOK) DrawText("mic offline — game effectively safe", 24, 66, 14, {200, 80, 80, 220});

            // Timer
            int mins = (int)(timeLeft / 60.0f);
            int secs = (int)timeLeft % 60;
            char tbuf[32]; std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mins, secs);
            int tw = MeasureText(tbuf, 36);
            DrawText(tbuf, SCR_W - tw - 24, 20, 36, {220, 220, 220, 220});
            DrawText("survive", SCR_W - 110, 60, 14, {140, 140, 140, 200});

            // Threat indicator
            const char* threat = "silence...";
            Color tc = {120, 160, 120, 220};
            if (stalkerTarget < 18.0f) { threat = "something moves..."; tc = {180, 160, 100, 220}; }
            if (stalkerTarget < 9.0f)  { threat = "IT IS NEAR";         tc = {220, 140, 60, 240}; }
            if (stalkerTarget < 4.0f)  { threat = "DON'T MOVE";         tc = {230, 50, 50, 240}; }
            DrawText(threat, 24, SCR_H - 40, 18, tc);

            // crosshair (tiny)
            DrawCircle(SCR_W / 2, SCR_H / 2, 1.5f, {200, 200, 200, 90});

            // hint fade
            if (now < 8.0f) {
                unsigned char a = (unsigned char)(220 * (1.0f - now / 8.0f));
                DrawText("WASD walk · mouse look · shift run · stay silent",
                         SCR_W / 2 - 250, SCR_H - 80, 18, {200, 200, 200, a});
            }
        }

        // Jumpscare red flash
        if (jumpscare > 0.0f) {
            unsigned char a = (unsigned char)(std::min(255.0f, jumpscare * 220.0f));
            DrawRectangle(0, 0, SCR_W, SCR_H, {180, 0, 0, a});
            DrawText("X", SCR_W / 2 - 60, SCR_H / 2 - 80, 160, {255, 240, 240, a});
        }

        // ---------------- screens ----------------
        if (state == State::Menu) {
            DrawRectangle(0, 0, SCR_W, SCR_H, {0, 0, 0, 220});
            const char* title = "SILENT WOODS";
            int tw = MeasureText(title, 64);
            DrawText(title, SCR_W / 2 - tw / 2, 140, 64, {170, 40, 40, 255});
            const char* sub = "you are lost. it hears every sound.";
            int sw = MeasureText(sub, 22);
            DrawText(sub, SCR_W / 2 - sw / 2, 230, 22, {180, 180, 180, 230});

            DrawText("WASD",  SCR_W / 2 - 200, 320, 22, {200,200,200,220});
            DrawText("walk",  SCR_W / 2 + 20,  320, 22, {140,140,140,220});
            DrawText("MOUSE", SCR_W / 2 - 200, 352, 22, {200,200,200,220});
            DrawText("look",  SCR_W / 2 + 20,  352, 22, {140,140,140,220});
            DrawText("SHIFT", SCR_W / 2 - 200, 384, 22, {200,200,200,220});
            DrawText("run (noisy!)",  SCR_W / 2 + 20, 384, 22, {140,140,140,220});
            DrawText("VOICE", SCR_W / 2 - 200, 416, 22, {200,200,200,220});
            DrawText("if it picks you up, it comes closer", SCR_W / 2 + 20, 416, 22, {200,120,120,220});

            const char* prompt = micOK ? "[ click or press SPACE to begin ]"
                                       : "[ NO MICROPHONE — game requires one ]";
            int pw = MeasureText(prompt, 22);
            DrawText(prompt, SCR_W / 2 - pw / 2, 510, 22, micOK ? Color{230,230,150,255} : Color{220,60,60,255});

            const char* tip = "headphones recommended · play in a dark room";
            int tipw = MeasureText(tip, 14);
            DrawText(tip, SCR_W / 2 - tipw / 2, SCR_H - 40, 14, {120,120,120,200});
        }

        if (state == State::Lost) {
            DrawRectangle(0, 0, SCR_W, SCR_H, {0, 0, 0, 230});
            const char* t = "IT FOUND YOU";
            int tw = MeasureText(t, 64);
            DrawText(t, SCR_W / 2 - tw / 2, 200, 64, {160, 30, 30, 255});
            const char* s = "you were too loud.";
            int sw = MeasureText(s, 22);
            DrawText(s, SCR_W / 2 - sw / 2, 290, 22, {180, 180, 180, 220});
            const char* p = "[ press R to try again ·  Esc to quit ]";
            int pw = MeasureText(p, 20);
            DrawText(p, SCR_W / 2 - pw / 2, 380, 20, {200, 200, 200, 220});
            if (IsKeyPressed(KEY_R)) { state = State::Menu; }
        }
        if (state == State::Won) {
            DrawRectangle(0, 0, SCR_W, SCR_H, {0, 0, 0, 220});
            const char* t = "DAWN";
            int tw = MeasureText(t, 80);
            DrawText(t, SCR_W / 2 - tw / 2, 200, 80, {180, 200, 160, 255});
            const char* s = "you survived the night.";
            int sw = MeasureText(s, 22);
            DrawText(s, SCR_W / 2 - sw / 2, 310, 22, {180, 180, 180, 220});
            const char* p = "[ press R to play again ]";
            int pw = MeasureText(p, 20);
            DrawText(p, SCR_W / 2 - pw / 2, 380, 20, {200, 200, 200, 220});
            if (IsKeyPressed(KEY_R)) { state = State::Menu; }
        }

        EndDrawing();

        if (IsKeyPressed(KEY_ESCAPE)) break;
    }

    ShutdownMic();
    UnloadSound(sndWhisper);
    UnloadSound(sndHeart);
    UnloadSound(sndBranch);
    UnloadSound(sndWind);
    UnloadSound(sndScreech);
    UnloadSound(sndOwl);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
