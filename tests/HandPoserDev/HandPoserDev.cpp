// HandPoserDev - in-game test of the Hand Poser plugin. Not shipped; installed only for a run.
// Loads the team room, takes a spare magazine from the player's pouch with the left hand (the game's slot grip, as in
// VR), holds it up where the arm reaches, and logs the hand and the magazine's grip once a second while the test script
// drives the editor's page (http://127.0.0.1:8765/api/...). Renders the hand and magazine at the start ("before") and
// when the script asks (it writes [Test] ShotDir\shoot_<name>.flag).
#include <GML/GML.hpp>
#include <windows.h>
#include <cmath>
#include <format>
#include <string>

using namespace gml;

GML_PLUGIN("com.remarrow.handposerdev", "HandPoserDev", "0.1.0", {{"com.remarrow.handposer", "0.1.0", GML_DEPENDENCY_SOFT}});

struct Vec { double x, y, z; };
struct Rot { double pitch, yaw, roll; };
struct XfD { double q[4]; double t[3]; double pad0; double s[3]; double pad1; };

static std::string s_shotDir;
static int s_step = -1, s_wait = 0, s_seconds = 0;
static Object s_ctrl, s_mag, s_cam, s_rt, s_cap;
static Vec s_at{};
static bool s_hold = false;

static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static Object Pawn() {
    Params p(Lib("GameplayStatics"), "GetPlayerPawn");
    p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("PlayerIndex", (int32_t)0).Call();
    return p.Return<GUObject*>();
}
static Vec ActorLocation(Object a) {
    Params p(a, "K2_GetActorLocation");
    p.Call();
    return p.Return<Vec>();
}
static Vec CompLocation(Object c) {
    Params p(c, "K2_GetComponentLocation");
    p.Call();
    return p.Return<Vec>();
}
static Vec BodyBone(const char* bone) {
    Params gl(Pawn().GetObj("Mesh"), "GetSocketLocation");
    *(uint64_t*)gl.Ptr("InSocketName") = API->MakeName(bone);
    gl.Call();
    return gl.Return<Vec>();
}
static Vec InMag(Vec world) {  // the magazine's frame
    Params gt(s_mag, "GetTransform");
    gt.Call();
    Params inv(Lib("KismetMathLibrary"), "InverseTransformLocation");
    std::memcpy(inv.Ptr("T"), gt.Ptr("ReturnValue"), sizeof(XfD));
    inv.Set("Location", world).Call();
    return inv.Return<Vec>();
}
static bool SlotGrip(Object ctrl, Object mag) {
    Object sock = mag.GetObj("HandSocket");
    Params hs(sock, "GetHandSocketTransform");
    hs.SetObj("QueryController", ctrl).Set("bIgnoreOnlySnapMesh", false).Call();
    Params gt(mag, "GetTransform");
    gt.Call();
    Params rel(Lib("KismetMathLibrary"), "MakeRelativeTransform");
    std::memcpy(rel.Ptr("A"), gt.Ptr("ReturnValue"), sizeof(XfD));
    std::memcpy(rel.Ptr("RelativeTo"), hs.Ptr("ReturnValue"), sizeof(XfD));
    rel.Call();
    Params g(ctrl, "GripObjectByInterface");
    g.SetObj("ObjectToGrip", mag);
    std::memcpy(g.Ptr("WorldOffset"), rel.Ptr("ReturnValue"), sizeof(XfD));
    *(uint64_t*)g.Ptr("OptionalSnapToSocketName") = API->MakeName(sock.Name().c_str());
    g.Set("bWorldOffsetIsRelative", true).Set("bIsSlotGrip", true).Call();
    return g.Return<bool>();
}
static void Hold() {
    if (!s_hold || !s_ctrl) return;
    Params mv(s_ctrl, "K2_SetWorldLocationAndRotation");
    mv.Set("NewLocation", s_at).Set("NewRotation", Rot{0, 0, 0}).Set("bSweep", false).Set("bTeleport", true).Call();
}
static void EnsureCapture() {
    if (s_rt) return;
    Params rt(Lib("KismetRenderingLibrary"), "CreateRenderTarget2D");
    rt.SetObj("WorldContextObject", Object(API->WorldContext())).Set("Width", (int32_t)1200).Set("Height", (int32_t)900).Set("Format", (uint8_t)2).Call();
    s_rt = rt.Return<GUObject*>();
    Params mk(Lib("KismetMathLibrary"), "MakeTransform");
    mk.Set("Scale", Vec{1, 1, 1}).Call();
    Params b(Lib("GameplayStatics"), "BeginDeferredActorSpawnFromClass");
    b.SetObj("WorldContextObject", Object(API->WorldContext())).Set("ActorClass", FindClass("SceneCapture2D")).Set("CollisionHandlingOverride", (uint8_t)1);
    std::memcpy(b.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), sizeof(XfD));
    b.Call();
    Object actor = b.Return<GUObject*>();
    Params f(Lib("GameplayStatics"), "FinishSpawningActor");
    f.SetObj("Actor", actor);
    std::memcpy(f.Ptr("SpawnTransform"), mk.Ptr("ReturnValue"), sizeof(XfD));
    f.Call();
    s_cap = actor.GetObj("CaptureComponent2D");
    s_cap.Set<GUObject*>("TextureTarget", s_rt.ptr);
    s_cap.SetBool("bCaptureEveryFrame", false);
    s_cap.Set("FOVAngle", 40.0f);
}
// The hand and the magazine from three sides around the magazine.
static void Shoot(const std::string& name) {
    EnsureCapture();
    s_cap.Set<uint8_t>("PrimitiveRenderMode", 2);
    if (auto* only = (GML_TArray*)s_cap.PropPtr("ShowOnlyActors")) {
        only->Num = 0;
        for (Object a : {Pawn(), s_mag}) *(GUObject**)API->ArrayAdd(only, sizeof(GUObject*), 1) = a.ptr;
    }
    Params gt(s_mag, "GetTransform");
    gt.Call();
    auto at = [&](Vec local) {
        Params f(Lib("KismetMathLibrary"), "TransformLocation");
        std::memcpy(f.Ptr("T"), gt.Ptr("ReturnValue"), sizeof(XfD));
        f.Set("Location", local).Call();
        return f.Return<Vec>();
    };
    Vec c = at({0, 0, -8});
    for (auto [view, local] : std::initializer_list<std::pair<const char*, Vec>>{{"a", {0, -40, -8}}, {"b", {0, 40, -8}}, {"c", {40, 0, -8}}}) {
        Vec from = at(local);
        Params look(Lib("KismetMathLibrary"), "FindLookAtRotation");
        look.Set("Start", from).Set("Target", c).Call();
        Params mv(Object(API->GetOuter(s_cap.ptr)), "K2_SetActorLocationAndRotation");
        mv.Set("NewLocation", from).Set("NewRotation", look.Return<Rot>()).Set("bSweep", false).Set("bTeleport", true).Call();
        s_cap.Set("CaptureSource", (uint8_t)7);
        Params cs(s_cap, "CaptureScene");
        cs.Call();
        Params ex(Lib("KismetRenderingLibrary"), "ExportRenderTarget");
        ex.SetObj("WorldContextObject", Object(API->WorldContext())).SetObj("TextureRenderTarget", s_rt)
            .SetString("FilePath", W(s_shotDir)).SetString("Filename", W("hand_" + name + "_" + view + ".png")).Call();
    }
    Log("SHOT hand_{}", name);
}
static void LogHand(const char* when) {
    Object sock = s_mag.GetObj("HandSocket");
    Vec l = sock.Get<Vec>("RelativeLocation");
    Rot r = sock.Get<Rot>("RelativeRotation");
    Vec w = InMag(BodyBone("hand_l")), tip = InMag(BodyBone("index_03_l"));
    Params gg(s_ctrl, "GetGrippedObjects");
    gg.Call();
    auto* arr = (GML_TArray*)gg.Ptr("GrippedObjectsArray");
    Log("HPDEV [{}] socket ({:.3f},{:.3f},{:.3f}) rot ({:.2f},{:.2f},{:.2f}); held {}; wrist in the magazine's frame ({:.2f},{:.2f},{:.2f}), index tip ({:.2f},{:.2f},{:.2f})",
        when, l.x, l.y, l.z, r.pitch, r.yaw, r.roll, arr && arr->Num > 0, w.x, w.y, w.z, tip.x, tip.y, tip.z);
}

static void Step() {
    if (s_wait > 0) { s_wait--; return; }
    switch (s_step) {
        case 0: {
            Object pawn = Pawn();
            Params gv(pawn, "GetVRComponents");
            gv.Call();
            s_ctrl = *(GUObject**)gv.Ptr("LeftController");
            s_cam = *(GUObject**)gv.Ptr("Camera");
            for (const char* cls : {"Magazine_AK_6L23_C", "Magazine_SR25_Steel_20_C", "GunMagazine"})
                for (auto& m : FindAllOf(cls)) {
                    if (s_mag || !m.GetObj("HandSocket")) continue;
                    Params pa(m, "GetAttachParentActor");
                    pa.Call();
                    if (Object(pa.Return<GUObject*>()).IsA("GunReceiver")) continue;  // seated in a gun
                    Object root = m;
                    for (int g = 0; g < 8; g++) {
                        Params up(root, "GetAttachParentActor");
                        up.Call();
                        Object u = up.Return<GUObject*>();
                        if (!u) break;
                        root = u;
                    }
                    if (root.ptr == pawn.ptr) s_mag = m;
                }
            if (!s_mag) { Error("HPDEV no spare magazine on the player"); s_step = 99; Log("DEV TEST COMPLETE"); return; }
            Log("HPDEV taking {} with the left hand", s_mag.Name());
            s_at = ActorLocation(s_mag);
            s_hold = true;
            s_step = 1; s_wait = 90;
            return;
        }
        case 1:
            Log("HPDEV slot grip: {}", SlotGrip(s_ctrl, s_mag));
            s_hold = false;
            s_step = 2; s_wait = 10;
            return;
        case 2: {  // up in front of the head, where the arm reaches
            Object pawn = Pawn();
            Params fw(pawn, "GetActorForwardVector");
            fw.Call();
            Params rt(pawn, "GetActorRightVector");
            rt.Call();
            Vec f = fw.Return<Vec>(), r = rt.Return<Vec>(), c = CompLocation(s_cam);
            s_at = {c.x + f.x * 25 - r.x * 15, c.y + f.y * 25 - r.y * 15, c.z - 30};
            s_hold = true;
            s_step = 3; s_wait = 60;
            return;
        }
        case 3:
            LogHand("holding");
            Shoot("before");
            Log("HPDEV holding - editor ready");
            s_step = 4; s_seconds = 0;
            return;
        case 4: {  // once a second: the hand, and any picture the script asks for
            s_wait = 60;
            LogHand(std::format("t{}", s_seconds).c_str());
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA((s_shotDir + "\\shoot_*.flag").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    std::string f = fd.cFileName, name = f.substr(6, f.size() - 11);
                    DeleteFileA((s_shotDir + "\\" + f).c_str());
                    Shoot(name);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
            if (GetFileAttributesA((s_shotDir + "\\drop.flag").c_str()) != INVALID_FILE_ATTRIBUTES) {  // the hand lets go, as
                DeleteFileA((s_shotDir + "\\drop.flag").c_str());                                       // opening Virtual Desktop's desktop does
                Params d(s_ctrl, "DropObjectByInterface");
                d.SetObj("ObjectToDrop", s_mag).Call();
                Log("HPDEV the hand let go");
            }
            if (GetFileAttributesA((s_shotDir + "\\done.flag").c_str()) != INVALID_FILE_ATTRIBUTES || ++s_seconds > 300) {
                DeleteFileA((s_shotDir + "\\done.flag").c_str());
                s_hold = false;
                s_step = 99;
                Log("DEV TEST COMPLETE");
            }
            return;
        }
    }
}

GML_AWAKE() {
    s_shotDir = Config.Bind<std::string>("Test", "ShotDir", "", "Where the captures go.").Value();
    On(GML_EVENT_WORLD_BEGIN_PLAY, [](void* gm) {
        std::string level = Object((GUObject*)gm).Path();
        if (level.find("/Game/Maps/MainMenu/") == 0) {
            Params p(Lib("GameplayStatics"), "OpenLevel");
            p.SetObj("WorldContextObject", Object(API->WorldContext())).Set("LevelName", API->MakeName("/Game/Maps/TeamRoom/Teamroom")).Set("bAbsolute", true).Call();
        } else if (level.find("Teamroom") != std::string::npos) {
            s_step = 0;
            s_wait = 600;
        }
    });
    On(GML_EVENT_TICK, [](void*) {
        if (s_step >= 0 && s_step < 99) Step();
        Hold();
    });
    return 0;
}
