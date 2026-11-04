// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "world/crew.h"
#include "world/world.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static const float kWalkSpeed[HS_CREW] = {0.036f, 0.022f, 0.018f, 0.024f, 0.030f, 0.028f};     // metres per second, each its own pace
static const float kStride[HS_CREW] = {0.0074f, 0.0086f, 0.0098f, 0.0123f, 0.0237f, 0.0066f};
static const float kFootStrength[HS_CREW] = {0.16f, 0.26f, 0.34f, 0.10f, 0.0f, 0.07f};

typedef struct {
  // World thread.
  float heading_x, heading_z, phase_distance, last_x, last_z;
  double stuck_since, next_wander;
  float target_x, target_z;
  hs_crew_job job;
  bool upright;
  int last_step_index;
  // Frame thread: springs for every joint, so weight and follow-through come from physics and not from a formula.
  float lean_x, lean_z, lean_vx, lean_vz;
  float look_yaw, look_pitch, look_vy, look_vp;
  float iris, arm_swing[2], arm_v[2];
  float work_phase;
} Member;

static Member g_crew[HS_CREW];
static hs_crew_station g_stations[HS_CREW];            // the world thread's copy
static hs_crew_station g_wanted[HS_CREW], g_mailbox[HS_CREW];
static bool g_wanted_dirty;
static _Atomic int g_mailbox_full;
static _Atomic float g_phase_shown[HS_CREW];
static _Atomic float g_awake;
static _Atomic float g_attention_x, g_attention_z, g_attention;
static _Atomic float g_foot_strength, g_foot_x;
static double g_cluster_since, g_cluster_worst;
static float g_cheer[HS_CREW];
static simd_float3 g_lamp;

void hs_crew_init(void) {
  atomic_store(&g_awake, 0.0f);
  memset(g_crew, 0, sizeof g_crew);
  for (int i = 0; i < HS_CREW; i++) {
    float a = (float)i * 1.047f + 0.4f;
    g_stations[i] = (hs_crew_station){cosf(a) * 0.015f, kHallCenterZ + sinf(a) * 0.015f, HS_JOB_WORK};
    g_wanted[i] = g_stations[i];
    atomic_store(&g_phase_shown[i], 0.0f);
    g_crew[i].heading_z = 1;
    g_crew[i].last_step_index = -1;
    g_crew[i].next_wander = 0;
  }
  g_cluster_since = g_cluster_worst = 0;
  g_wanted_dirty = false;
  atomic_store(&g_mailbox_full, 0);
}

void hs_crew_set_stations(const hs_crew_station stations[HS_CREW]) {
  memcpy(g_wanted, stations, sizeof g_wanted);
  g_wanted_dirty = true;
}
void hs_crew_set_awake(float awake) { atomic_store(&g_awake, awake); }

void hs_crew_set_attention(float x, float z, float strength) {
  atomic_store(&g_attention_x, x);
  atomic_store(&g_attention_z, z);
  atomic_store(&g_attention, strength);
}

static inline float hash01(int a, int b) {
  unsigned h = (unsigned)a * 2654435761u + (unsigned)b * 40503u;
  h ^= h >> 13;
  h *= 2246822519u;
  h ^= h >> 16;
  return (float)(h & 0xFFFF) / 65535.0f;
}

void hs_crew_step(const float felt[3], float dt, double now_ms) {
  if (atomic_load_explicit(&g_mailbox_full, memory_order_acquire)) {
    memcpy(g_stations, g_mailbox, sizeof g_stations);
    atomic_store_explicit(&g_mailbox_full, 0, memory_order_release);
  }
  float felt_size = sqrtf(felt[0] * felt[0] + felt[1] * felt[1] + felt[2] * felt[2]);
  float slope = felt_size > 0.01f ? acosf(fminf(1, fmaxf(-1, felt[1] / felt_size))) : 0;
  bool steady_ground = slope < 24.0f * (float)M_PI / 180 && fabsf(felt_size - 1) < 0.6f;
  float attention = atomic_load(&g_attention), ax = atomic_load(&g_attention_x), az = atomic_load(&g_attention_z);
  float px[HS_CREW], pz[HS_CREW];
  for (int i = 0; i < HS_CREW; i++) {
    float position[3], rotation[4], velocity[3];
    hs_world_pose(i, position, rotation, velocity);
    px[i] = position[0];
    pz[i] = position[2];
  }
  for (int i = 0; i < HS_CREW; i++) {
    Member *m = &g_crew[i];
    if (!steady_ground) {
      hs_world_steer(i, false, 0, 0, 0, 1);
      m->upright = false;
      continue;
    }
    m->upright = true;
    float goal_x = g_stations[i].x, goal_z = g_stations[i].z;
    if (attention > 0.2f && i != HS_OLA) { goal_x = ax + cosf((float)i * 1.3f) * 0.008f; goal_z = az + sinf((float)i * 1.3f) * 0.008f; }
    if (g_stations[i].job == HS_JOB_IDLE && now_ms > m->next_wander) {
      // Each stays round its own place in the room, so that wandering can never become a crowd.
      m->target_x = g_stations[i].x + (hash01(i, (int)(now_ms / 1000)) - 0.5f) * 0.009f;
      m->target_z = g_stations[i].z + (hash01(i + 7, (int)(now_ms / 1000)) - 0.5f) * 0.009f;
      m->next_wander = now_ms + 3000 + 5000 * hash01(i + 3, (int)(now_ms / 1000));
    }
    if (g_stations[i].job == HS_JOB_IDLE && attention <= 0.2f) { goal_x = m->target_x; goal_z = m->target_z; }
    float dx = goal_x - px[i], dz = goal_z - pz[i], distance = sqrtf(dx * dx + dz * dz);
    float want = distance > 0.0015f ? fminf(kWalkSpeed[i], distance * 3.0f) : 0;
    float vx = distance > 1e-5f ? dx / distance * want : 0, vz = distance > 1e-5f ? dz / distance * want : 0;
    // Others ahead push the path sideways, so two who meet pass each other instead of stopping.
    for (int j = 0; j < HS_CREW; j++) {
      if (j == i) continue;
      float ox = px[j] - px[i], oz = pz[j] - pz[i], d2 = ox * ox + oz * oz;
      if (d2 > 0.012f * 0.012f || d2 < 1e-9f) continue;
      float d = sqrtf(d2), press = (0.012f - d) / 0.012f;
      vx += (-ox / d - oz / d * 0.6f) * press * kWalkSpeed[i] * 1.4f;
      vz += (-oz / d + ox / d * 0.6f) * press * kWalkSpeed[i] * 1.4f;
    }
    // Too many close by, and it steps out of the group whatever it was doing.
    float gx = 0, gz = 0;
    int close = 0;
    for (int j = 0; j < HS_CREW; j++) {
      float ox = px[j] - px[i], oz = pz[j] - pz[i];
      if (j != i && ox * ox + oz * oz < 0.015f * 0.015f) { gx += ox; gz += oz; close++; }
    }
    if (close >= 2 && attention <= 0.2f) {
      float size = sqrtf(gx * gx + gz * gz), push = close >= 3 ? 1.2f : 0.4f;
      if (size > 1e-5f) { vx -= gx / size * kWalkSpeed[i] * push; vz -= gz / size * kWalkSpeed[i] * push; }
    }
    float speed = sqrtf(vx * vx + vz * vz);
    float awake = atomic_load(&g_awake);
    if (awake < 0.3f) { vx = vz = 0; speed = 0; }
    if (speed > 0.002f) { m->heading_x = vx / speed; m->heading_z = vz / speed; }
    hs_world_steer(i, true, vx, vz, m->heading_x, m->heading_z);
    // Stride follows the distance really covered, so feet do not slide, and each footfall is one event in the hands.
    float moved = sqrtf((px[i] - m->last_x) * (px[i] - m->last_x) + (pz[i] - m->last_z) * (pz[i] - m->last_z));
    if (moved < 0.01f) m->phase_distance += moved;
    atomic_store_explicit(&g_phase_shown[i], m->phase_distance, memory_order_relaxed);
    m->last_x = px[i];
    m->last_z = pz[i];
    int step_index = (int)floorf(m->phase_distance / (kStride[i] * 0.5f));
    if (step_index != m->last_step_index) {
      m->last_step_index = step_index;
      if (kFootStrength[i] > 0) {
        atomic_store(&g_foot_strength, fmaxf(atomic_load(&g_foot_strength), kFootStrength[i]));
        atomic_store(&g_foot_x, px[i]);
      }
    }
  }
  // The gate's measure: how long four or more have stood within 15 mm of one spot.
  bool clustered = false;
  for (int i = 0; i < HS_CREW && !clustered; i++) {
    int near = 0;
    for (int j = 0; j < HS_CREW; j++) if ((px[j] - px[i]) * (px[j] - px[i]) + (pz[j] - pz[i]) * (pz[j] - pz[i]) < 0.015f * 0.015f) near++;
    if (near >= 4) clustered = true;
  }
  if (clustered && attention <= 0.2f && atomic_load(&g_awake) > 0.9f) {
    if (g_cluster_since == 0) g_cluster_since = now_ms;
    double held = (now_ms - g_cluster_since) / 1000;
    if (held > g_cluster_worst) g_cluster_worst = held;
  } else {
    g_cluster_since = 0;
  }
  (void)dt;
}

static simd_float4x4 compose(simd_float3 at, simd_quatf q) {
  simd_float4x4 m = simd_matrix4x4(q);
  m.columns[3] = simd_make_float4(at, 1);
  return m;
}

static simd_quatf axis_angle(float x, float y, float z, float a) { return simd_quaternion(a, simd_make_float3(x, y, z)); }

static inline void spring(float *value, float *speed, float target, float stiffness, float damping, float dt) {
  *speed += (target - *value) * stiffness * dt - *speed * damping * dt;
  *value += *speed * dt;
}

// Joints in millimetres from the feet: where the body and head ride, where limbs hang. Matches model/instrument/crew.py.
typedef struct { float body_y, head_y, head_z, arm_x, arm_y, leg_x, leg_y, leg_length; } Skeleton;
#define K 0.82f
static const Skeleton kBones[HS_CREW] = {
  [HS_PIP] = {3.9f * K, 1.45f * K, 0.0f, 0, 0, 1.15f * K, 3.9f * K, 3.9f * K},
  [HS_TAM] = {6.8f * K, 0.5f * K, 0.0f, 5.35f * K, 1.5f * K, 2.35f * K, 3.5f * K, 3.5f * K},
  [HS_MOX] = {8.1f * K, 0.0f, 0.0f, 7.45f * K, 0.6f * K, 3.0f * K, 3.1f * K, 3.0f * K},
  [HS_OLA] = {6.2f * K, 9.4f * K, 0.0f, 0.55f * K, 5.9f * K, 0.85f * K, 5.2f * K, 5.2f * K},
  [HS_BO] = {11.3f * K, 0.4f * K, 0.0f, 3.9f * K, 0.6f * K, 0, 0, 0},
  [HS_WREN] = {5.6f * K, 1.2f * K, 0.0f, 2.1f * K, -0.4f * K, 2.1f * K, -0.4f * K, 6.4f * K},
};
#undef K

static void place_limbs(int who, simd_float4x4 *b, simd_float3 at, simd_float3 feet, simd_float3 up, simd_quatf bq, float leg_swing, float idle, float work, float phase, float lean_x, float work_phase, float going) {
  const Skeleton *k = &kBones[who];
    simd_float3 arm_l = simd_make_float3(-k->arm_x, k->arm_y, 0) * 0.001f, arm_r = simd_make_float3(k->arm_x, k->arm_y, 0) * 0.001f;
    simd_float3 leg_l = simd_make_float3(-k->leg_x, k->leg_y - k->body_y, 0) * 0.001f, leg_r = simd_make_float3(k->leg_x, k->leg_y - k->body_y, 0) * 0.001f;
    switch (who) {
      case HS_PIP:
        b[4] = compose(at + simd_act(bq, leg_l), simd_mul(bq, axis_angle(1, 0, 0, leg_swing)));
        b[5] = compose(at + simd_act(bq, leg_r), simd_mul(bq, axis_angle(1, 0, 0, -leg_swing)));
        break;
      case HS_TAM:
        b[2] = compose(at + simd_act(bq, arm_l), simd_mul(bq, axis_angle(1, 0, 0, -leg_swing * 0.8f + idle)));
        b[3] = compose(at + simd_act(bq, arm_r), simd_mul(bq, axis_angle(1, 0, 0, leg_swing * 0.8f - 1.0f + work * 0.35f)));
        b[4] = compose(at + simd_act(bq, leg_l), simd_mul(bq, axis_angle(1, 0, 0, leg_swing)));
        b[5] = compose(at + simd_act(bq, leg_r), simd_mul(bq, axis_angle(1, 0, 0, -leg_swing)));
        break;
      case HS_MOX:
        b[2] = compose(at + simd_act(bq, arm_l), simd_mul(bq, axis_angle(1, 0, 0, -leg_swing * 0.6f + idle)));
        b[3] = compose(at + simd_act(bq, arm_r), simd_mul(bq, axis_angle(1, 0, 0, work_phase * (going < 0.2f ? 1.6f : 0.3f))));
        b[4] = compose(at + simd_act(bq, leg_l), simd_mul(bq, axis_angle(1, 0, 0, leg_swing)));
        b[5] = compose(at + simd_act(bq, leg_r), simd_mul(bq, axis_angle(1, 0, 0, -leg_swing)));
        break;
      case HS_OLA:
        b[2] = compose(at + simd_act(bq, arm_l), simd_mul(bq, simd_mul(axis_angle(0, 0, 1, -0.45f + idle), axis_angle(1, 0, 0, -leg_swing * 0.4f))));
        b[3] = compose(at + simd_act(bq, arm_r), simd_mul(bq, simd_mul(axis_angle(0, 0, 1, 0.45f - idle), axis_angle(1, 0, 0, leg_swing * 0.4f))));
        b[4] = compose(at + simd_act(bq, leg_l), simd_mul(bq, axis_angle(1, 0, 0, leg_swing)));
        b[5] = compose(at + simd_act(bq, leg_r), simd_mul(bq, axis_angle(1, 0, 0, -leg_swing)));
        break;
      case HS_BO: {
        float balance = sinf(work_phase * 2.1f) * 0.3f + lean_x * 2;
        b[6] = compose(feet + up * 0.0038f, simd_mul(bq, axis_angle(1, 0, 0, phase)));
        b[2] = compose(at + simd_act(bq, arm_l), simd_mul(bq, axis_angle(0, 0, 1, -1.1f + balance * 0.5f)));
        b[3] = compose(at + simd_act(bq, arm_r), simd_mul(bq, axis_angle(0, 0, 1, 1.1f + balance * 0.5f)));
        break;
      }
      default: {  // four legs in a diagonal gait
        const float xs[4] = {-k->arm_x, k->arm_x, -k->arm_x, k->arm_x}, zs[4] = {1.3f, 1.3f, -1.3f, -1.3f}, side[4] = {1, -1, -1, 1};
        for (int leg = 0; leg < 4; leg++) {
          simd_quatf spread = simd_mul(axis_angle(0, 0, 1, xs[leg] > 0 ? 0.60f : -0.60f), axis_angle(1, 0, 0, (zs[leg] > 0 ? -0.5f : 0.5f) + side[leg] * leg_swing));
          b[2 + leg] = compose(at + simd_act(bq, simd_make_float3(xs[leg], k->leg_y, zs[leg]) * 0.001f), simd_mul(bq, spread));
        }
        break;
      }
    }
}

void hs_crew_pose(hs_scene *scene, simd_float3 eye, float dt) {
  float awake = atomic_load(&g_awake);
  if (g_wanted_dirty && !atomic_load_explicit(&g_mailbox_full, memory_order_acquire)) {
    memcpy(g_mailbox, g_wanted, sizeof g_mailbox);
    atomic_store_explicit(&g_mailbox_full, 1, memory_order_release);
    g_wanted_dirty = false;
  }
  int capsules = scene->capsule_count;
  for (int who = 0; who < HS_CREW; who++) {
    Member *m = &g_crew[who];
    const Skeleton *k = &kBones[who];
    float position[3], rotation[4], velocity[3];
    hs_world_pose(who, position, rotation, velocity);
    simd_quatf body_q = simd_quaternion(rotation[0], rotation[1], rotation[2], rotation[3]);
    simd_float3 p = simd_make_float3(position[0], position[1], position[2]);
    simd_float3 up = simd_act(body_q, simd_make_float3(0, 1, 0));
    // The physics body is a capsule whose foot sits at the floor when upright.
    simd_float3 feet = p - up * hs_world_foot_drop(who);

    float speed = sqrtf(velocity[0] * velocity[0] + velocity[2] * velocity[2]);
    float phase = atomic_load_explicit(&g_phase_shown[who], memory_order_relaxed) / kStride[who] * 2 * (float)M_PI;
    float going = fminf(1, speed / (kWalkSpeed[who] * 0.5f));
    float swing = sinf(phase) * going;
    // Mid stance the foot must move backward exactly as fast as the body moves forward, or it slides.
    float leg_swing = k->leg_length > 0 ? swing * fminf(0.7f, kStride[who] * 1000 / (2 * (float)M_PI * k->leg_length)) : 0;
    float bob = (fabsf(cosf(phase)) - 0.6f) * going * 0.0005f;

    // Leaning into acceleration, a spring's delay behind it: anticipation and follow-through from physics.
    spring(&m->lean_x, &m->lean_vx, velocity[0] * 7.0f, 90, 11, dt);
    spring(&m->lean_z, &m->lean_vz, velocity[2] * 7.0f, 90, 11, dt);
    simd_quatf lean = simd_mul(axis_angle(1, 0, 0, m->lean_z), axis_angle(0, 0, 1, -m->lean_x));

    // Gaze: the head turns to the player whenever nothing else matters.
    simd_float3 to_eye = simd_normalize(eye - p);
    simd_float3 face = simd_act(body_q, simd_make_float3(0, 0, 1));
    float yaw_error = atan2f(simd_dot(simd_cross(face, to_eye), up), simd_dot(face, to_eye));
    float look_yaw = fmaxf(-1.5f, fminf(1.5f, yaw_error)) * awake;
    float look_pitch = fminf(0.8f, asinf(fmaxf(-1, fminf(1, simd_dot(to_eye, up))))) * awake;
    spring(&m->look_yaw, &m->look_vy, look_yaw, 150, 17, dt);
    spring(&m->look_pitch, &m->look_vp, look_pitch, 150, 17, dt);
    m->iris += (awake - m->iris) * (1 - expf(-dt * 3));
    m->work_phase += dt * (1 + 0.5f * (float)who);

    // A small figure looking up at someone leans back to do it.
    simd_quatf bq = simd_mul(simd_mul(body_q, lean), axis_angle(1, 0, 0, -m->look_pitch * 0.32f));
    simd_float3 at = feet + up * (k->body_y * 0.001f + bob);
    simd_float4x4 *b = scene->crew_bones[who];
    simd_quatf look = simd_mul(axis_angle(0, 1, 0, m->look_yaw), axis_angle(1, 0, 0, -m->look_pitch));
    simd_float3 head_at = at + simd_act(bq, simd_make_float3(0, k->head_y, k->head_z) * 0.001f);
    b[0] = compose(at, bq);
    b[1] = compose(head_at, simd_mul(bq, look));
    if (who == HS_OLA) g_lamp = head_at;
    float idle = sinf(m->work_phase * 0.9f) * 0.06f * (1 - going);
    g_cheer[who] *= expf(-dt * 1.6f);
    float raised = g_cheer[who] * g_cheer[who] * (2 + 0.5f * sinf(m->work_phase * 11));
    idle -= raised;
    float working = g_wanted[who].job == HS_JOB_WORK && going < 0.2f ? 1 : 0;
    float work = sinf(m->work_phase * 3) * working;
    place_limbs(who, b, at, feet, up, bq, leg_swing, idle, work, phase, m->lean_x, m->work_phase, going);
    if (capsules < HS_MAX_CAPSULES) {
      simd_float4 a = simd_mul(scene->object, simd_make_float4(feet + up * 0.001f, 1)), c = simd_mul(scene->object, simd_make_float4(feet + up * k->body_y * 0.0012f, 1));
      scene->capsules[capsules].a = simd_make_float4(a.xyz, 0.0042f);
      scene->capsules[capsules].b = simd_make_float4(c.xyz, 0.9f);
      {
        // Far from a capsule the shader only needs this one test, and most of the picture is far from every capsule.
        float reach = 0.0042f * 3.2f + simd_length(c.xyz - a.xyz) * 0.5f;
        scene->capsules[capsules].reach = simd_make_float4((a.xyz + c.xyz) * 0.5f, reach * reach);
      }
      capsules++;
    }
  }
  scene->capsule_count = capsules;
}

void hs_crew_cheer(int who) {
  if (who < 0 || who >= HS_CREW) return;
  g_cheer[who] = 1;
  hs_world_nudge(who, 0, 0.05f, 0);
}

void hs_crew_rest(hs_scene *scene, simd_float3 eye) {
  // At their ease in the hall, for the Home window, where nothing simulates them.
  static const float places[HS_CREW][2] = {{-17, 9}, {17, 7}, {-20, -6}, {19, -6}, {-5, 15}, {7, 17}};
  for (int who = 0; who < HS_CREW; who++) {
    const Skeleton *k = &kBones[who];
    simd_float3 feet = simd_make_float3(places[who][0] * 0.001f, kHallFloorY, kHallCenterZ + places[who][1] * 0.001f);
    simd_float3 up = simd_make_float3(0, 1, 0);
    simd_float3 to_eye = simd_normalize(eye - feet);
    simd_quatf turn = axis_angle(0, 1, 0, atan2f(to_eye.x, to_eye.z));
    simd_quatf bq = simd_mul(turn, axis_angle(1, 0, 0, -0.2f));
    simd_float3 at = feet + up * (k->body_y * 0.001f);
    simd_float4x4 *b = scene->crew_bones[who];
    b[0] = compose(at, bq);
    simd_float3 head_at = at + simd_act(bq, simd_make_float3(0, k->head_y, k->head_z) * 0.001f);
    b[1] = compose(head_at, simd_mul(bq, axis_angle(1, 0, 0, -0.45f)));
    place_limbs(who, b, at, feet, up, bq, 0, 0, 0, 0, 0, (float)who, 0);
  }
}

hs_crew_events hs_crew_take_events(void) {
  hs_crew_events e = {atomic_exchange(&g_foot_strength, 0.0f), atomic_load(&g_foot_x)};
  return e;
}

float hs_crew_lamp(simd_float3 *where) {
  *where = g_lamp;
  return atomic_load(&g_awake);
}

void hs_crew_cluster_report(double *worst_seconds) { *worst_seconds = g_cluster_worst; }
