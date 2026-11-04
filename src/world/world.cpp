// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "world/world.h"
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace JPH;

namespace {
// Jolt asks for small arrays while it steps: its tree of bodies is rebuilt from time to time. On the block thread that must not reach the
// system's allocator, so everything it asks for comes from one block of memory taken up front and kept on free lists by size.
constexpr size_t kPoolBytes = 32 * 1024 * 1024, kBlockHeader = 64;
constexpr int kSizeClasses = 26;
struct PoolBlock { PoolBlock *next; };
char *g_pool, *g_pool_next, *g_pool_end;
PoolBlock *g_free[kSizeClasses];
long g_pool_misses;

int size_class(size_t bytes) {
  int c = 0;
  size_t size = 64;
  while (size < bytes && c < kSizeClasses - 1) { size <<= 1; c++; }
  return c;
}

void *pool_allocate(size_t bytes) {
  int c = size_class(bytes);
  size_t size = size_t(64) << c;
  if (PoolBlock *b = g_free[c]) {
    g_free[c] = b->next;
    return b;
  }
  if (g_pool_next + size + kBlockHeader > g_pool_end) {        // never in a run: the pool is several times what the hall needs
    g_pool_misses++;
    void *fallback = nullptr;
    if (posix_memalign(&fallback, 64, size + kBlockHeader) != 0) return nullptr;
    *static_cast<int *>(fallback) = c;
    return static_cast<char *>(fallback) + kBlockHeader;
  }
  char *block = g_pool_next;
  g_pool_next += size + kBlockHeader;
  *reinterpret_cast<int *>(block) = c;
  return block + kBlockHeader;
}

void pool_free(void *memory) {
  if (!memory) return;
  char *block = static_cast<char *>(memory) - kBlockHeader;
  int c = *reinterpret_cast<int *>(block);
  PoolBlock *b = static_cast<PoolBlock *>(memory);
  b->next = g_free[c];
  g_free[c] = b;
}

void *pool_reallocate(void *memory, size_t old_size, size_t new_size) {
  void *moved = pool_allocate(new_size);
  if (memory && moved) {
    memcpy(moved, memory, std::min(old_size, new_size));
    pool_free(memory);
  }
  return moved;
}

void *pool_aligned_allocate(size_t bytes, size_t) { return pool_allocate(bytes); }   // every block is on a 64-byte boundary

void use_pool(void) {
  if (!g_pool) {
    if (posix_memalign(reinterpret_cast<void **>(&g_pool), 64, kPoolBytes) != 0) abort();
    memset(g_pool, 0, kPoolBytes);     // touched now, so that no page fault can happen in a block
  }
  g_pool_next = g_pool;
  g_pool_end = g_pool + kPoolBytes;
  std::fill(g_free, g_free + kSizeClasses, nullptr);
  JPH::Allocate = pool_allocate;
  JPH::Reallocate = pool_reallocate;
  JPH::Free = pool_free;
  JPH::AlignedAllocate = pool_aligned_allocate;
  JPH::AlignedFree = pool_free;
}

// Small things in a metre-based solver behave badly, so the hall is simulated forty times larger.
constexpr float Scale = 40.0f;
const float EarthG = 9.81f * kWorldSlowMotion;
constexpr float MostFelt = 30.0f;                  // m/s^2, whatever the shaking: no force can drive a body through a wall
constexpr float Fastest = 1.1f;                    // m/s
constexpr int MaxHits = 96;

namespace Layers { constexpr ObjectLayer STILL = 0, MOVING = 1; }
namespace BPLayers { constexpr BroadPhaseLayer STILL(0), MOVING(1); constexpr uint COUNT = 2; }

class LayerPairs final : public ObjectLayerPairFilter {
public:
  bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == Layers::MOVING || b == Layers::MOVING; }
};
class BroadPhaseLayers final : public BroadPhaseLayerInterface {
public:
  uint GetNumBroadPhaseLayers() const override { return BPLayers::COUNT; }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override { return layer == Layers::STILL ? BPLayers::STILL : BPLayers::MOVING; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "layer"; }
#endif
};
class LayerToBroadPhase final : public ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer broad) const override { return layer == Layers::MOVING || broad == BPLayers::MOVING; }
};

struct World;
World *W = nullptr;

enum CommandKind { CMD_NUDGE, CMD_PLACE, CMD_BALL, CMD_DRAG, CMD_WELL };
struct Command { CommandKind kind; int body; float a, b, c; };
constexpr unsigned kCommands = 64;
Command g_commands[kCommands];
std::atomic<unsigned> g_command_head{0}, g_command_tail{0};

// One writer, several readers on other threads, and no waiting for either: a reader that sees the count change while it copies copies again.
struct Pose { std::atomic<float> position[3], rotation[4], velocity[3]; };
Pose g_poses[HS_WORLD_BODIES];
std::atomic<unsigned> g_pose_count{0};

void send(Command c) {
  unsigned head = g_command_head.load(std::memory_order_relaxed);
  if (head - g_command_tail.load(std::memory_order_acquire) >= kCommands) return;
  g_commands[head % kCommands] = c;
  g_command_head.store(head + 1, std::memory_order_release);
}

class Contacts final : public ContactListener {
public:
  void OnContactAdded(const Body &a, const Body &b, const ContactManifold &m, ContactSettings &) override;
};

struct World {
  TempAllocatorImpl temp{4 * 1024 * 1024};
  JobSystemSingleThreaded jobs{cMaxPhysicsJobs};
  BroadPhaseLayers broad;
  LayerToBroadPhase layerToBroad;
  LayerPairs pairs;
  Contacts listener;
  PhysicsSystem system;
  BodyID bodies[HS_WORLD_BODIES];
  BodyID well;
  bool wellOpen = false;
  struct { bool on; Vec3 walk, face; } steer[HS_WORLD_BODIES] = {};
  bool ballActive = false;
  hs_world_hit hits[MaxHits];
  int hitCount = 0;
  Vec3 lastTurn = Vec3::sZero();
  long rescues = 0;
  float drag[HS_WORLD_BODIES] = {};
};

// Each body's own size, low weight and all: the crew right themselves like roly-poly toys.
const float kRadius[HS_WORLD_BODIES] = {0.0030f, 0.0040f, 0.0050f, 0.0024f, 0.0034f, 0.0034f, 0.0022f};
const float kHalfHeight[HS_WORLD_BODIES] = {0.0012f, 0.0014f, 0.0018f, 0.0036f, 0.0024f, 0.0010f, 0.0f};
const float kMass[HS_WORLD_BODIES] = {0.8f, 1.6f, 2.2f, 0.7f, 1.2f, 0.7f, 0.5f};

void Contacts::OnContactAdded(const Body &a, const Body &b, const ContactManifold &m, ContactSettings &) {
  if (W->hitCount >= MaxHits) return;
  RVec3 point = m.GetWorldSpaceContactPointOn1(0);
  float closing = (a.GetPointVelocity(point) - b.GetPointVelocity(point)).Dot(m.mWorldSpaceNormal);
  if (closing <= 0) return;
  bool aMoves = a.GetMotionType() == EMotionType::Dynamic, bMoves = b.GetMotionType() == EMotionType::Dynamic;
  hs_world_hit_kind kind = aMoves && bMoves ? HS_HIT_EACH_OTHER : std::abs(m.mWorldSpaceNormal.GetY()) > 0.7f ? HS_HIT_FLOOR : HS_HIT_WALL;
  int body = (int)(aMoves ? a.GetUserData() : b.GetUserData());
  W->hits[W->hitCount++] = {kind, body, closing / Scale, float(point.GetX()) / Scale, float(point.GetZ()) / Scale};
}

RVec3 spawn(int i) {
  float a = float(i) * 1.047f + 0.4f, r = i == HS_WORLD_BALL ? 0.004f : 0.015f;
  return RVec3(std::cos(a) * r * Scale, (kHallFloorY + 0.008f) * Scale, (kHallCenterZ + std::sin(a) * r) * Scale);
}
}  // namespace

extern "C" {

void hs_world_create(void) {
  use_pool();
  Factory::sInstance = new Factory();
  RegisterTypes();
  W = new World();
  W->system.Init(64, 0, 256, 256, W->broad, W->layerToBroad, W->pairs);
  PhysicsSettings settings = W->system.GetPhysicsSettings();
  settings.mAllowSleeping = false;
  W->system.SetPhysicsSettings(settings);
  W->system.SetContactListener(&W->listener);
  BodyInterface &bi = W->system.GetBodyInterfaceNoLock();
  auto still = [&](Vec3 half, Vec3 center, Quat q) {
    BodyCreationSettings s(new BoxShape(half * Scale, 0.01f), RVec3(center * Scale), q, EMotionType::Static, Layers::STILL);
    s.mFriction = 0.55f;
    s.mRestitution = 0.45f;
    bi.CreateAndAddBody(s, EActivation::DontActivate);
  };
  still(Vec3(0.05f, 0.005f, 0.05f), Vec3(0, kHallFloorY - 0.005f, kHallCenterZ), Quat::sIdentity());     // the floor
  still(Vec3(0.05f, 0.005f, 0.05f), Vec3(0, 0.0205f, kHallCenterZ), Quat::sIdentity());                 // under the dome
  still(Vec3(0.0150f, 0.0040f, 0.0058f), Vec3(0, -0.0014f, -0.0250f), Quat::sIdentity());                // the music box
  {
    // With the lid up there is a hole in the floor where the speaker sits. Nothing may stand or roll across thin air: it becomes a kerb.
    BodyCreationSettings s(new CylinderShape(0.012f * Scale, 0.0125f * Scale, 0.01f), RVec3(Vec3(0, kHallFloorY + 0.005f, kHallCenterZ) * Scale), Quat::sIdentity(), EMotionType::Static, Layers::STILL);
    s.mFriction = 0.3f;
    s.mRestitution = 0.3f;
    W->well = bi.CreateBody(s)->GetID();
  }
  enum { kWallPieces = 20 };
  for (int i = 0; i < kWallPieces; i++) {                                                               // a round wall, as 20 flat ones
    float a = float(i) * 6.2832f / kWallPieces;
    Vec3 center(std::cos(a) * (kHallRadius + 0.004f), 0.008f, kHallCenterZ + std::sin(a) * (kHallRadius + 0.004f));
    still(Vec3(0.004f, 0.016f, 0.0065f), center, Quat::sRotation(Vec3::sAxisY(), -a));
  }
  for (int i = 0; i < HS_WORLD_BODIES; i++) {
    Ref<Shape> shape;
    if (i == HS_WORLD_BALL) {
      shape = new SphereShape(kRadius[i] * Scale);
    } else {
      Ref<Shape> capsule = CapsuleShapeSettings(kHalfHeight[i] * Scale, kRadius[i] * Scale).Create().Get();
      shape = OffsetCenterOfMassShapeSettings(Vec3(0, -(kHalfHeight[i] + 0.45f * kRadius[i]) * Scale, 0), capsule).Create().Get();
    }
    BodyCreationSettings s(shape, spawn(i), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
    s.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    s.mMassPropertiesOverride.mMass = kMass[i];
    s.mFriction = i == HS_WORLD_BALL ? 0.25f : 0.5f;
    s.mRestitution = i == HS_WORLD_BALL ? 0.3f : 0.4f;
    s.mLinearDamping = 0.05f;
    s.mAngularDamping = i == HS_WORLD_BALL ? 0.08f : 0.35f;
    s.mMotionQuality = EMotionQuality::LinearCast;
    s.mMaxLinearVelocity = Fastest * Scale;
    s.mAllowSleeping = false;
    s.mUserData = (uint64)i;
    W->bodies[i] = bi.CreateBody(s)->GetID();
    if (i != HS_WORLD_BALL) bi.AddBody(W->bodies[i], EActivation::Activate);
  }
  W->system.OptimizeBroadPhase();
  g_command_tail.store(g_command_head.load());
  for (int i = 0; i < HS_WORLD_BODIES; i++) {
    RVec3 p = spawn(i);
    for (int side = 0; side < 2; side++) {
      for (int a = 0; a < 3; a++) { g_poses[i].position[a] = float(p[a]) / Scale; g_poses[i].velocity[a] = 0; }
      g_poses[i].rotation[0] = g_poses[i].rotation[1] = g_poses[i].rotation[2] = 0;
      g_poses[i].rotation[3] = 1;
    }
  }
}

void hs_world_destroy(void) {
  if (!W) return;
  delete W;
  W = nullptr;
  UnregisterTypes();
  delete Factory::sInstance;
  Factory::sInstance = nullptr;
}

void hs_world_set_ball_active(bool active) { send({CMD_BALL, HS_WORLD_BALL, active ? 1.0f : 0.0f, 0, 0}); }
void hs_world_set_well_open(bool open) { send({CMD_WELL, 0, open ? 1.0f : 0.0f, 0, 0}); }
void hs_world_set_drag(int body, float per_second) { send({CMD_DRAG, body, per_second, 0, 0}); }

static void set_ball(bool active) {
  if (active == W->ballActive) return;
  BodyInterface &bi = W->system.GetBodyInterfaceNoLock();
  W->ballActive = active;
  if (active) {
    bi.SetPositionAndRotation(W->bodies[HS_WORLD_BALL], spawn(HS_WORLD_BALL), Quat::sIdentity(), EActivation::DontActivate);
    bi.SetLinearAndAngularVelocity(W->bodies[HS_WORLD_BALL], Vec3::sZero(), Vec3::sZero());
    bi.AddBody(W->bodies[HS_WORLD_BALL], EActivation::Activate);
  } else {
    bi.RemoveBody(W->bodies[HS_WORLD_BALL]);
  }
}

static void run_commands(void) {
  BodyInterface &bi = W->system.GetBodyInterfaceNoLock();
  unsigned tail = g_command_tail.load(std::memory_order_relaxed), head = g_command_head.load(std::memory_order_acquire);
  for (; tail != head; tail++) {
    const Command &c = g_commands[tail % kCommands];
    if (c.body < 0 || c.body >= HS_WORLD_BODIES) continue;
    if (c.kind == CMD_BALL) { set_ball(c.a > 0.5f); continue; }
    if (c.kind == CMD_WELL) {
      if ((c.a > 0.5f) != W->wellOpen) {
        W->wellOpen = c.a > 0.5f;
        if (W->wellOpen) bi.AddBody(W->well, EActivation::DontActivate);
        else bi.RemoveBody(W->well);
      }
      continue;
    }
    if (c.kind == CMD_DRAG) { W->drag[c.body] = c.a; continue; }
    if (c.body == HS_WORLD_BALL && !W->ballActive) continue;
    if (c.kind == CMD_NUDGE) {
      bi.AddLinearVelocity(W->bodies[c.body], Vec3(c.a, c.b, c.c) * Scale);
    } else {
      bi.SetPosition(W->bodies[c.body], RVec3(c.a, c.b, c.c) * Scale, EActivation::Activate);
      bi.SetLinearAndAngularVelocity(W->bodies[c.body], Vec3::sZero(), Vec3::sZero());
    }
  }
  g_command_tail.store(tail, std::memory_order_release);
}

static void publish(void) {
  BodyInterface &bi = W->system.GetBodyInterfaceNoLock();
  g_pose_count.fetch_add(1, std::memory_order_acq_rel);     // odd: being written
  int count = W->ballActive ? HS_WORLD_BODIES : HS_WORLD_BALL;
  for (int i = 0; i < count; i++) {
    RVec3 p;
    Quat q;
    bi.GetPositionAndRotation(W->bodies[i], p, q);
    Vec3 v = bi.GetLinearVelocity(W->bodies[i]);
    Pose &out = g_poses[i];
    for (int a = 0; a < 3; a++) {
      out.position[a].store(float(p[a]) / Scale, std::memory_order_relaxed);
      out.velocity[a].store(v[a] / Scale, std::memory_order_relaxed);
    }
    out.rotation[0].store(q.GetX(), std::memory_order_relaxed);
    out.rotation[1].store(q.GetY(), std::memory_order_relaxed);
    out.rotation[2].store(q.GetZ(), std::memory_order_relaxed);
    out.rotation[3].store(q.GetW(), std::memory_order_relaxed);
  }
  g_pose_count.fetch_add(1, std::memory_order_acq_rel);     // even: whole again
}

void hs_world_step(const float felt[3], const float turn[3], float dt) {
  run_commands();
  BodyInterface &bi = W->system.GetBodyInterfaceNoLock();
  Vec3 felt_now(felt[0], felt[1], felt[2]);
  float heaviest = MostFelt / EarthG;
  if (felt_now.Length() > heaviest) felt_now = felt_now.Normalized() * heaviest;
  W->system.SetGravity(felt_now * (-EarthG * Scale));
  Vec3 w(turn[0], turn[1], turn[2]);
  Vec3 alpha = (w - W->lastTurn) / dt;
  W->lastTurn = w;
  int count = W->ballActive ? HS_WORLD_BODIES : HS_WORLD_BALL;
  for (int i = 0; i < count; i++) {
    Vec3 r = Vec3(bi.GetPosition(W->bodies[i]));
    Vec3 v = bi.GetLinearVelocity(W->bodies[i]);
    Vec3 inertial = -w.Cross(w.Cross(r)) - 2.0f * w.Cross(v) - alpha.Cross(r);
    float most = MostFelt * Scale;
    if (inertial.Length() > most) inertial = inertial.Normalized() * most;
    bi.AddForce(W->bodies[i], (inertial - v * W->drag[i]) * kMass[i]);
    if (W->steer[i].on) {
      // Balance and turning, as a stiff, well damped spring on its attitude.
      Vec3 up = -W->system.GetGravity().NormalizedOr(-Vec3::sAxisY());
      Quat q = bi.GetRotation(W->bodies[i]);
      Vec3 spin = bi.GetAngularVelocity(W->bodies[i]);
      Vec3 lean = (q * Vec3::sAxisY()).Cross(up);
      Vec3 wanted = W->steer[i].face - up * W->steer[i].face.Dot(up);
      wanted = wanted.NormalizedOr(q * Vec3::sAxisZ());
      float yaw = up.Dot((q * Vec3::sAxisZ()).Cross(wanted));
      float inertia = kMass[i] * 0.4f * (kRadius[i] * Scale) * (kRadius[i] * Scale);
      bi.AddTorque(W->bodies[i], (lean * 260.0f + up * (yaw * 90.0f) - spin * 24.0f) * inertia);
      // The feet carry it: across the floor only, with a limit to how sharply feet can push.
      // Feet hold on a slope they can stand on, so gravity along the floor reaches them only as a lean and a shuffle.
      Vec3 gravity = W->system.GetGravity();
      bi.AddForce(W->bodies[i], -(gravity - up * gravity.Dot(up)) * (0.85f * kMass[i]));
      Vec3 across = v - up * v.Dot(up);
      Vec3 push = (W->steer[i].walk * Scale - across) * 30.0f;
      float most_push = 3.0f * Scale;
      if (push.Length() > most_push) push = push.Normalized() * most_push;
      bi.AddForce(W->bodies[i], push * kMass[i]);
    }
  }
  W->system.Update(dt, 1, &W->temp, &W->jobs);
  // However violent the shaking, a body must never end up with invalid numbers or outside the hall.
  for (int i = 0; i < count; i++) {
    Vec3 p = Vec3(bi.GetPosition(W->bodies[i]));
    Vec3 v = bi.GetLinearVelocity(W->bodies[i]);
    Vec3 from_center = p / Scale - Vec3(0, 0.005f, kHallCenterZ);
    if (p.IsNaN() || v.IsNaN() || from_center.Length() > 0.06f) {
      bi.SetPositionAndRotation(W->bodies[i], spawn(i), Quat::sIdentity(), EActivation::Activate);
      bi.SetLinearAndAngularVelocity(W->bodies[i], Vec3::sZero(), Vec3::sZero());
      W->rescues++;
    }
  }
  publish();
}

void hs_world_steer(int body, bool on, float vx, float vz, float face_x, float face_z) {
  if (body < 0 || body >= HS_WORLD_BODIES) return;
  auto &s = W->steer[body];
  if (on != s.on) W->system.GetBodyInterfaceNoLock().SetFriction(W->bodies[body], on ? 0.02f : 0.5f);    // the feet do the work
  s.on = on;
  s.walk = Vec3(vx, 0, vz);
  s.face = Vec3(face_x, 0, face_z);
}

void hs_world_nudge(int body, float vx, float vy, float vz) { send({CMD_NUDGE, body, vx, vy, vz}); }
void hs_world_place(int body, float x, float y, float z) { send({CMD_PLACE, body, x, y, z}); }

void hs_world_pose(int body, float position[3], float rotation[4], float velocity[3]) {
  const Pose &p = g_poses[body];
  unsigned before;
  do {
    before = g_pose_count.load(std::memory_order_acquire);
    for (int a = 0; a < 3; a++) {
      position[a] = p.position[a].load(std::memory_order_relaxed);
      velocity[a] = p.velocity[a].load(std::memory_order_relaxed);
    }
    for (int a = 0; a < 4; a++) rotation[a] = p.rotation[a].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
  } while ((before & 1) || before != g_pose_count.load(std::memory_order_relaxed));
}

int hs_world_take_hits(hs_world_hit *out, int capacity) {
  int n = std::min(W->hitCount, capacity);
  std::copy(W->hits, W->hits + n, out);
  W->hitCount = 0;
  return n;
}

float hs_world_foot_drop(int body) { return kHalfHeight[body] + kRadius[body]; }

long hs_world_rescues(void) { return W ? W->rescues : 0; }

}
