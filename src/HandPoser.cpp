// HandPoser - GML plugin (com.remarrow.handposer): make your own hand poses for what you hold, live.
//
// Hold a magazine in the game and open http://127.0.0.1:8765/ in a browser (on the PC, in Virtual Desktop's desktop
// view, or from a phone with [Server] AllowLan). The page shows each hand's magazine and its grip: where the hand sits
// on it (the magazine's hand socket: its position and rotation on the magazine) and every finger joint (the socket's
// pose deltas, each joint's turn from the open hand). Every change goes onto the magazine at once and the hand takes it
// again, so you see it on your own hand in the headset a third of a second later. Save keeps the pose, for every
// magazine of that kind (its class; a magazine another plugin has re-dressed, like the Saiga's Sb.5 or the Barrett's,
// is its own kind), in GML\config\com.remarrow.handposer.cfg, and puts it on each one from then on.
//
// The game places your hand from the socket when it takes the magazine (its IK target, its fingers), so a change
// shows after the hand lets go and takes the magazine again: the plugin does that itself, holding the magazine still
// for the moment between.
#include <GML/GML.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "page.h"
#pragma comment(lib, "ws2_32.lib")

using namespace gml;

GML_PLUGIN("com.remarrow.handposer", "Hand Poser", "0.1.0");

struct Vec3 { double x, y, z; };
struct Rot3 { double pitch, yaw, roll; };
struct XfD { double q[4]; double t[3]; double pad0; double s[3]; double pad1; };
static_assert(sizeof(XfD) == 0x60);

// ------------------------------------------------------------------ rotations (FRotator <-> FQuat, as Unreal does)

static constexpr double kPi = 3.14159265358979323846;
static std::array<double, 4> RotToQuat(double pitch, double yaw, double roll) {
    double h = kPi / 360;
    double sp = std::sin(pitch * h), cp = std::cos(pitch * h), sy = std::sin(yaw * h), cy = std::cos(yaw * h), sr = std::sin(roll * h),
           cr = std::cos(roll * h);
    return {cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy};
}
static double NormAxis(double a) {
    a = std::fmod(a, 360);
    if (a > 180) a -= 360;
    if (a < -180) a += 360;
    return a;
}
static std::array<double, 3> QuatToRot(const double* q) {
    double x = q[0], y = q[1], z = q[2], w = q[3], d = 180 / kPi;
    double test = z * x - w * y, yawY = 2 * (w * z + x * y), yawX = 1 - 2 * (y * y + z * z);
    if (test < -0.4999995) {
        double yaw = std::atan2(yawY, yawX) * d;
        return {-90, yaw, NormAxis(-yaw - 2 * std::atan2(x, w) * d)};
    }
    if (test > 0.4999995) {
        double yaw = std::atan2(yawY, yawX) * d;
        return {90, yaw, NormAxis(yaw - 2 * std::atan2(x, w) * d)};
    }
    return {std::asin(2 * test) * d, std::atan2(yawY, yawX) * d, std::atan2(-2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * d};
}

// ------------------------------------------------------------------ a pose: the socket on the magazine, the fingers

struct BonePose { std::string name; std::array<double, 4> q; };
struct Pose {
    Vec3 loc{};
    Rot3 rot{};
    std::vector<BonePose> bones;
};

// FBPVRHandPoseBonePair: the bone's FName, its delta FQuat at +0x10, 0x40 in all.
struct PairRaw { uint64_t bone, pad; double q[4]; uint8_t rest[0x10]; };
static_assert(sizeof(PairRaw) == 0x40);

static std::string NameStr(uint64_t n) {
    char b[128] = "";
    API->NameToString(n, b, sizeof b);
    return b;
}

static bool DeltasLayoutOk(Object sock) {
    static int ok = -1;
    if (ok < 0) {
        GML_PropInfo ai{}, ei{};
        GFProperty* p = API->FindProperty(sock.Struct(), "CustomPoseDeltas");
        ok = p && API->GetPropertyInfo(p, &ai) && ai.inner && API->GetPropertyInfo(ai.inner, &ei) && ei.size == (int)sizeof(PairRaw);
        if (!ok) Warn("the hand socket's finger poses aren't in the expected layout - only the hand's position can be edited");
    }
    return ok == 1;
}

static Pose ReadPose(Object sock) {
    Pose p;
    p.loc = sock.Get<Vec3>("RelativeLocation");
    p.rot = sock.Get<Rot3>("RelativeRotation");
    if (!DeltasLayoutOk(sock)) return p;
    if (auto* d = (GML_TArray*)sock.PropPtr("CustomPoseDeltas"))
        for (int i = 0; i < d->Num; i++) {
            auto& r = ((PairRaw*)d->Data)[i];
            p.bones.push_back({NameStr(r.bone), {r.q[0], r.q[1], r.q[2], r.q[3]}});
        }
    return p;
}

static void WritePose(Object sock, const Pose& p) {
    Params s(sock, "K2_SetRelativeLocationAndRotation");
    s.Set("NewLocation", p.loc).Set("NewRotation", p.rot).Set("bSweep", false).Set("bTeleport", true).Call();
    if (!DeltasLayoutOk(sock)) return;
    auto* d = (GML_TArray*)sock.PropPtr("CustomPoseDeltas");
    if (!d) return;
    for (auto& b : p.bones) {
        PairRaw* r = nullptr;
        for (int i = 0; i < d->Num && !r; i++)
            if (NameStr(((PairRaw*)d->Data)[i].bone) == b.name) r = &((PairRaw*)d->Data)[i];
        if (!r) continue;  // a bone this socket doesn't pose
        for (int k = 0; k < 4; k++) r->q[k] = b.q[k];
    }
}

static bool Same(const Pose& a, const Pose& b) {
    auto close = [](double x, double y, double e) { return std::fabs(x - y) < e; };
    if (!close(a.loc.x, b.loc.x, 1e-3) || !close(a.loc.y, b.loc.y, 1e-3) || !close(a.loc.z, b.loc.z, 1e-3)) return false;
    auto qa = RotToQuat(a.rot.pitch, a.rot.yaw, a.rot.roll), qb = RotToQuat(b.rot.pitch, b.rot.yaw, b.rot.roll);
    double dot = std::fabs(qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3]);
    if (dot < 1 - 1e-7) return false;
    for (auto& x : b.bones)
        for (auto& y : a.bones)
            if (x.name == y.name) {
                double d2 = std::fabs(x.q[0] * y.q[0] + x.q[1] * y.q[1] + x.q[2] * y.q[2] + x.q[3] * y.q[3]);
                if (d2 < 1 - 1e-7) return false;
            }
    return true;
}

// "loc x y z; rot p y r; bone name qx qy qz qw; ..."
static std::string Encode(const Pose& p) {
    std::string s = std::format("loc {:.4f} {:.4f} {:.4f}; rot {:.4f} {:.4f} {:.4f}", p.loc.x, p.loc.y, p.loc.z, p.rot.pitch, p.rot.yaw, p.rot.roll);
    for (auto& b : p.bones) s += std::format("; bone {} {:.6f} {:.6f} {:.6f} {:.6f}", b.name, b.q[0], b.q[1], b.q[2], b.q[3]);
    return s;
}
static bool Decode(const std::string& s, Pose& p) {
    p = {};
    bool loc = false;
    size_t a = 0;
    while (a < s.size()) {
        size_t b = s.find(';', a);
        std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        a = b == std::string::npos ? s.size() : b + 1;
        char kind[16] = "", name[96] = "";
        double v[4] = {};
        if (sscanf_s(part.c_str(), " %15s", kind, (unsigned)sizeof kind) != 1) continue;
        if (!strcmp(kind, "loc") && sscanf_s(part.c_str(), " loc %lf %lf %lf", &v[0], &v[1], &v[2]) == 3) { p.loc = {v[0], v[1], v[2]}; loc = true; }
        else if (!strcmp(kind, "rot") && sscanf_s(part.c_str(), " rot %lf %lf %lf", &v[0], &v[1], &v[2]) == 3) p.rot = {v[0], v[1], v[2]};
        else if (!strcmp(kind, "bone") && sscanf_s(part.c_str(), " bone %95s %lf %lf %lf %lf", name, (unsigned)sizeof name, &v[0], &v[1], &v[2], &v[3]) == 5)
            p.bones.push_back({name, {v[0], v[1], v[2], v[3]}});
    }
    return loc;
}

// ------------------------------------------------------------------ what you hold

static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}

// Only this kind of mod gives a magazine DynamicMeshComponents (another plugin dressed it: the Sb.5, the Barrett's).
static bool Dressed(Object actor) {
    Params gc(actor, "K2_GetComponentsByClass");
    gc.Set("ComponentClass", FindClass("DynamicMeshComponent")).Call();
    auto* arr = (GML_TArray*)gc.Ptr("ReturnValue");
    return arr && arr->Num > 0;
}
// What a pose is saved under: the magazine's class, "@mod" if another plugin re-dressed it.
static std::string KeyOf(Object item) {
    return Object((GUObject*)item.Class()).Name() + (Dressed(item) ? "@mod" : "");
}
static std::string LabelOf(Object item) {
    std::string c = Object((GUObject*)item.Class()).Name();
    if (c.size() > 2 && c.ends_with("_C")) c.resize(c.size() - 2);
    if (Dressed(item)) c += " (re-dressed by a mod)";
    return c;
}

// The editor's view of one hand: the magazine it is editing (kept while the plugin lets go and takes it again).
struct Edit {
    GUObject* ctrl = nullptr;
    GUObject* item = nullptr;
    GUObject* sock = nullptr;
    std::string key;
    bool dirty = false;
    bool held = false;  // in the hand now (or let go, still being edited)
    // taking it again: 0 none, 1 waiting for the edits to settle, 2 let go (waiting frames)
    int regrip = 0;
    ULONGLONG due = 0;
    int frames = 0;
};
static Edit s_edit[2];  // 0 left, 1 right

static Object Controller(bool left) {
    Object pawn = Pawn();
    if (!pawn) return {};
    Params gv(pawn, "GetVRComponents");
    gv.Call();
    return *(GUObject**)gv.Ptr(left ? "LeftController" : "RightController");
}
// The magazine (anything with a "HandSocket") a controller holds, if any.
static Object HeldBy(Object ctrl) {
    if (!ctrl) return {};
    Params gg(ctrl, "GetGrippedObjects");
    gg.Call();
    auto* arr = (GML_TArray*)gg.Ptr("GrippedObjectsArray");
    for (int i = 0; arr && i < arr->Num; i++) {
        Object o(((GUObject**)arr->Data)[i]);
        if (o && o.GetObj("HandSocket")) return o;
    }
    return {};
}

// A VRExpansion slot grip, as the game's own: the magazine's hand socket onto the controller.
static bool SlotGrip(Object ctrl, Object item) {
    Object sock = item.GetObj("HandSocket");
    Params hs(sock, "GetHandSocketTransform");
    hs.SetObj("QueryController", ctrl).Set("bIgnoreOnlySnapMesh", false).Call();
    Params gt(item, "GetTransform");
    gt.Call();
    Params rel(Lib("KismetMathLibrary"), "MakeRelativeTransform");
    std::memcpy(rel.Ptr("A"), gt.Ptr("ReturnValue"), sizeof(XfD));
    std::memcpy(rel.Ptr("RelativeTo"), hs.Ptr("ReturnValue"), sizeof(XfD));
    rel.Call();
    Params g(ctrl, "GripObjectByInterface");
    g.SetObj("ObjectToGrip", item);
    std::memcpy(g.Ptr("WorldOffset"), rel.Ptr("ReturnValue"), sizeof(XfD));
    *(uint64_t*)g.Ptr("OptionalSnapToSocketName") = API->MakeName(sock.Name().c_str());
    g.Set("bWorldOffsetIsRelative", true).Set("bIsSlotGrip", true).Call();
    return g.Return<bool>();
}

// ------------------------------------------------------------------ saved poses

static std::map<std::string, Pose> s_saved;                          // key -> your pose
static std::map<std::pair<GUObject*, std::string>, Pose> s_original;  // (socket, key) -> as it was before yours

static ConfigEntry<std::string> PoseEntry(const std::string& key) {
    return Config.Bind<std::string>("Poses", key.c_str(), "", "Your hand pose for this kind of magazine (written by the editor).");
}
static ConfigEntry<std::string> IndexEntry() {
    return Config.Bind<std::string>("Poses", "Saved", "", "The kinds with a saved pose (written by the editor).");
}
static void LoadSaved() {
    std::string index = IndexEntry().Value();
    for (size_t a = 0; a < index.size();) {
        size_t b = index.find(',', a);
        std::string key = index.substr(a, b == std::string::npos ? std::string::npos : b - a);
        a = b == std::string::npos ? index.size() : b + 1;
        Pose p;
        if (!key.empty() && Decode(PoseEntry(key).Value(), p)) s_saved[key] = p;
    }
    Log("{} saved pose(s)", s_saved.size());
}
static void StoreIndex() {
    std::string index;
    for (auto& [k, _] : s_saved) index += (index.empty() ? "" : ",") + k;
    IndexEntry().Set(index);
}

static void Regrip(Edit& e) {
    if (e.regrip == 2) return;  // already let go: it is taken again with the latest values
    e.regrip = 1;
    e.due = GetTickCount64() + 150;
}

// The original pose of a socket under a key, before any of yours went on it.
static const Pose& Original(Object sock, const std::string& key) {
    auto k = std::make_pair(sock.ptr, key);
    auto it = s_original.find(k);
    if (it == s_original.end()) it = s_original.emplace(k, ReadPose(sock)).first;
    return it->second;
}

// Every half second: your saved poses onto every magazine of their kind (another plugin may have re-dressed one, or
// fitted its own grip, since).
static void ApplySaved() {
    static ULONGLONG next = 0;
    if (s_saved.empty() || GetTickCount64() < next) return;
    next = GetTickCount64() + 500;
    std::map<std::string, std::vector<Object>> byClass;
    for (auto& [key, pose] : s_saved) {
        std::string cls = key.substr(0, key.find('@'));
        if (!byClass.count(cls)) byClass[cls] = FindAllOf(cls.c_str());
        for (auto& item : byClass[cls]) {
            if (!API->IsValid(item.ptr) || KeyOf(item) != key) continue;
            Object sock = item.GetObj("HandSocket");
            if (!sock) continue;
            bool editing = false;
            for (auto& e : s_edit)
                if (e.item == item.ptr && e.dirty) editing = true;
            if (editing) continue;  // your unsaved changes on the one in your hand stay
            Original(sock, key);
            if (Same(ReadPose(sock), pose)) continue;
            WritePose(sock, pose);
            for (auto& e : s_edit)
                if (e.item == item.ptr) Regrip(e);
        }
    }
}

// ------------------------------------------------------------------ the editor's commands (from the page)

static std::mutex s_mtx;
static std::deque<std::string> s_commands;  // query strings, run on the game thread
static std::string s_state = "{\"hands\":[]}";

static std::string QueryValue(const std::string& q, const char* name) {
    std::string key = std::string(name) + "=";
    for (size_t a = 0; a < q.size();) {
        size_t b = q.find('&', a);
        std::string part = q.substr(a, b == std::string::npos ? std::string::npos : b - a);
        a = b == std::string::npos ? q.size() : b + 1;
        if (part.rfind(key, 0) == 0) return part.substr(key.size());
    }
    return {};
}

static void RunCommand(const std::string& q) {
    std::string handStr = QueryValue(q, "hand"), op = QueryValue(q, "op");
    int h = handStr == "left" ? 0 : handStr == "right" ? 1 : -1;
    if (h < 0) return;
    Edit& e = s_edit[h];
    if (!e.sock || !API->IsValid(e.sock) || !API->IsValid(e.item)) return;
    Object sock(e.sock);
    Pose p = ReadPose(sock);
    double v = atof(QueryValue(q, "v").c_str());
    int i = std::clamp(atoi(QueryValue(q, "i").c_str()), 0, 2);
    if (op == "set") {
        std::string f = QueryValue(q, "f");
        if (f == "loc") (&p.loc.x)[i] = v;
        else if (f == "rot") (&p.rot.pitch)[i] = v;
        else if (f == "bone") {
            std::string name = QueryValue(q, "b");
            for (auto& b : p.bones)
                if (b.name == name) {
                    auto r = QuatToRot(b.q.data());
                    r[i] = v;
                    b.q = RotToQuat(r[0], r[1], r[2]);
                }
        } else return;
        WritePose(sock, p);
        e.dirty = true;
        Regrip(e);
    } else if (op == "curl") {  // every joint's turn from the open hand, scaled
        double k = std::clamp(v, 0.1, 3.0);
        for (auto& b : p.bones) {
            double sgn = b.q[3] < 0 ? -1 : 1, half = std::acos(std::clamp(std::fabs(b.q[3]), 0.0, 1.0)), s = std::sin(half);
            if (s < 1e-9) continue;
            double na = std::clamp(half * k, 0.0, kPi / 2), f = std::sin(na) / s;
            for (int j = 0; j < 3; j++) b.q[j] *= sgn * f;
            b.q[3] = std::cos(na);
        }
        WritePose(sock, p);
        e.dirty = true;
        Regrip(e);
    } else if (op == "take") {  // back into the hand that let go of it
        e.regrip = 2;
        e.frames = 15;
    } else if (op == "save") {
        Original(sock, e.key);
        s_saved[e.key] = p;
        PoseEntry(e.key).Set(Encode(p));
        StoreIndex();
        e.dirty = false;
        Log("saved your pose for {}: {}", e.key, Encode(p));
    } else if (op == "revert") {  // back to your saved pose, or the default
        auto it = s_saved.find(e.key);
        WritePose(sock, it != s_saved.end() ? it->second : Original(sock, e.key));
        e.dirty = false;
        Regrip(e);
    } else if (op == "reset") {  // forget yours: the default on every one of them
        s_saved.erase(e.key);
        PoseEntry(e.key).Set("");
        StoreIndex();
        for (auto& [k, orig] : s_original)
            if (k.second == e.key && API->IsValid(k.first)) WritePose(Object(k.first), orig);
        e.dirty = false;
        Regrip(e);
        Log("forgot your pose for {}", e.key);
    }
}

// ------------------------------------------------------------------ every frame

static std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if ((unsigned char)c >= 0x20) o += c;
    }
    return o;
}

static void Track() {
    for (int h = 0; h < 2; h++) {
        Edit& e = s_edit[h];
        if (e.regrip == 2) {  // let go: take it again after the hand has settled
            if (++e.frames < 15) continue;
            e.regrip = 0;
            if (API->IsValid(e.item) && API->IsValid(e.ctrl)) {
                if (Object root = Object(e.item).GetObj("RootComponent")) {  // as a dropped magazine is, before the grip takes it
                    Params sp(root, "SetSimulatePhysics");
                    sp.Set("bSimulate", true).Call();
                }
                bool ok = SlotGrip(Object(e.ctrl), Object(e.item));
                if (!ok) Warn("could not take {} again", Object(e.item).Name());
            }
            continue;
        }
        if (e.regrip == 1 && GetTickCount64() >= e.due && API->IsValid(e.item) && API->IsValid(e.ctrl)) {
            Object item(e.item);
            Params d(Object(e.ctrl), "DropObjectByInterface");
            d.SetObj("ObjectToDrop", item).Call();
            if (Object root = item.GetObj("RootComponent")) {  // hold it still for the moment between
                Params sp(root, "SetSimulatePhysics");
                sp.Set("bSimulate", false).Call();
            }
            e.regrip = 2;
            e.frames = 0;
            continue;
        }
        if (e.regrip) continue;
        Object ctrl = Controller(h == 0), item = HeldBy(ctrl);
        if (!item) {
            // let go (opening Virtual Desktop's desktop to reach the page lets go too): the editor keeps the magazine,
            // and any change, or "Put it back in my hand", puts it back
            if (e.item && !API->IsValid(e.item)) e = {};
            else if (e.item && e.held) {
                e.held = false;
                Log("{} hand let go of {} - still editing it", h == 0 ? "left" : "right", Object(e.item).Name());
            }
            continue;
        }
        e.held = true;
        if (item.ptr != e.item) {
            e = {};
            e.ctrl = ctrl.ptr;
            e.item = item.ptr;
            e.sock = item.GetObj("HandSocket").ptr;
            e.key = KeyOf(item);
            Original(Object(e.sock), e.key);
            e.held = true;
            Log("{} hand holds {} ({})", h == 0 ? "left" : "right", item.Name(), e.key);
        }
    }
}

static void PublishState() {
    static ULONGLONG next = 0;
    if (GetTickCount64() < next) return;
    next = GetTickCount64() + 100;
    std::string j = "{\"hands\":[";
    bool first = true;
    for (int h = 0; h < 2; h++) {
        Edit& e = s_edit[h];
        if (!e.sock || !API->IsValid(e.sock) || !API->IsValid(e.item)) continue;
        Pose p = ReadPose(Object(e.sock));
        std::string sig = std::format("{:x}", (uintptr_t)e.item);
        std::string bones;
        for (auto& b : p.bones) {
            auto r = QuatToRot(b.q.data());
            bones += std::format("{}{{\"name\":\"{}\",\"rot\":[{:.3f},{:.3f},{:.3f}]}}", bones.empty() ? "" : ",", JsonEscape(b.name), r[0], r[1], r[2]);
            sig += "," + b.name;
        }
        j += std::format("{}{{\"hand\":\"{}\",\"label\":\"{}\",\"key\":\"{}\",\"sig\":\"{}\",\"saved\":{},\"dirty\":{},\"loc\":[{:.4f},{:.4f},{:.4f}],"
                         "\"held\":{},\"rot\":[{:.3f},{:.3f},{:.3f}],\"bones\":[{}]}}",
                         first ? "" : ",", h == 0 ? "left" : "right", JsonEscape(LabelOf(Object(e.item))), JsonEscape(e.key), sig,
                         s_saved.count(e.key) ? "true" : "false", e.dirty ? "true" : "false", p.loc.x, p.loc.y, p.loc.z, e.held ? "true" : "false", p.rot.pitch,
                         p.rot.yaw, p.rot.roll, bones);
        first = false;
    }
    j += "]}";
    std::lock_guard g(s_mtx);
    s_state = j;
}

// ------------------------------------------------------------------ the page's server (its own thread; the game's
// objects are only touched on the game thread, through s_commands and s_state)

static std::atomic<bool> s_stop{false};
static SOCKET s_listen = INVALID_SOCKET;

static void Reply(SOCKET c, const char* type, const std::string& body, const char* status = "200 OK") {
    std::string h = std::format("HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status,
                                type, body.size());
    send(c, h.data(), (int)h.size(), 0);
    send(c, body.data(), (int)body.size(), 0);
}

static void Serve(int port, bool lan) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return;
    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
    if (s_listen == INVALID_SOCKET || bind(s_listen, (sockaddr*)&a, sizeof a) || listen(s_listen, 8)) {
        Error("the editor page could not open port {} (in use?) - set [Server] Port", port);
        return;
    }
    Log("editor: http://127.0.0.1:{}/{}", port, lan ? " (also open on your network)" : "");
    std::string page;
    for (const char* p : kPageParts) page += p;
    while (!s_stop) {
        SOCKET c = accept(s_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD timeout = 2000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
        char buf[4096];
        int n = recv(c, buf, sizeof buf - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string req(buf), path;
            size_t sp1 = req.find(' '), sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
            if (sp2 != std::string::npos) path = req.substr(sp1 + 1, sp2 - sp1 - 1);
            if (path == "/" || path == "/index.html") Reply(c, "text/html; charset=utf-8", page);
            else if (path == "/api/state") {
                std::string s;
                { std::lock_guard g(s_mtx); s = s_state; }
                Reply(c, "application/json", s);
            } else if (path.rfind("/api/cmd?", 0) == 0) {
                { std::lock_guard g(s_mtx); if (s_commands.size() < 256) s_commands.push_back(path.substr(9)); }
                Reply(c, "text/plain", "ok");
            } else Reply(c, "text/plain", "not found", "404 Not Found");
        }
        closesocket(c);
    }
}

// ------------------------------------------------------------------ entry

GML_AWAKE() {
    int port = std::clamp(Config.Bind("Server", "Port", 8765, "The editor page's port: http://127.0.0.1:<port>/").Value(), 1024, 65535);
    bool lan = Config.Bind("Server", "AllowLan", false, "Also serve the page to other devices on your network (a phone or tablet).").Value();
    LoadSaved();
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void*) {
        for (auto& e : s_edit) e = {};
        s_original.clear();  // a new level's objects can reuse old addresses
    });
    On(GML_EVENT_TICK, [](void*) {
        std::deque<std::string> cmds;
        { std::lock_guard g(s_mtx); cmds.swap(s_commands); }
        for (auto& c : cmds) RunCommand(c);
        Track();
        ApplySaved();
        PublishState();
    });
    std::thread(Serve, port, lan).detach();
    return 0;
}
