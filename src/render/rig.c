// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "render/rig.h"
#include <math.h>
#include <string.h>

static simd_float4x4 about_pivot(simd_float3 pivot, simd_quatf turn, simd_float3 slide) {
  simd_float4x4 t = matrix_identity_float4x4, back = matrix_identity_float4x4;
  t.columns[3] = simd_make_float4(pivot + slide, 1);
  back.columns[3] = simd_make_float4(-pivot, 1);
  return simd_mul(simd_mul(t, simd_matrix4x4(turn)), back);
}

static simd_float3 vec(const float v[3]) { return simd_make_float3(v[0], v[1], v[2]); }

// A part's own motion, from its name. Each is one of: turning on its axis by an amount, or sliding along it.
static simd_float4x4 local_motion(const hs_part *part, const hs_scene *s) {
  const char *name = part->name;
  simd_float3 pivot = vec(part->pivot), axis = vec(part->axis);
  simd_quatf none = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  float amount = 0;
  bool turns = true;
  if (!strncmp(name, "stick_l", 7) || !strncmp(name, "stick_r", 7)) {
    int side = name[6] == 'r';
    float limit = part->travel[1];
    simd_quatf tilt = simd_mul(simd_quaternion(s->sticks[side][0] * limit, simd_make_float3(0, 0, -1)),
                               simd_quaternion(s->sticks[side][1] * limit, simd_make_float3(1, 0, 0)));
    return about_pivot(pivot, tilt, simd_make_float3(0, -s->stick_press[side] * 0.0012f, 0));
  }
  if (!strncmp(name, "bellows_", 8)) {
    // Paper folds open along its axis from the plate, and whatever sits on top rides up with it.
    int side = name[8] == 'r';
    float height = part->travel[0] + (part->travel[1] - part->travel[0]) * s->bellows[side];
    simd_float4x4 m = matrix_identity_float4x4;
    if (name[9] != '_') {
      m.columns[1].y = height;
      m.columns[3].y = pivot.y * (1 - height);
      return m;
    }
    m.columns[3].y = 0.0072f * (height - 1);
    return m;
  }
  if (!strncmp(name, "coil_", 5) && (name[7] == 'w' || name[7] == 'f')) {
    // The magnet's blow answers in what it pushes against, a little.
    simd_float4x4 m = matrix_identity_float4x4;
    m.columns[3].z = -s->coil[name[5] == 'r'] * part->travel[1] * 0.15f;
    return m;
  }
  if (!strncmp(name, "coil_", 5) && (name[7] == 'm' || name[7] == 's')) {
    // The magnet slides on two springs, which stretch from their fixed stops.
    int side = name[5] == 'r';
    float move = s->coil[side] * part->travel[1];
    simd_float4x4 m = matrix_identity_float4x4;
    if (name[7] == 'm') { m.columns[3].z = move; return m; }
    float length = 0.0039f, gap = name[14] == 'a' ? length - move : length + move;
    float scale = gap / length;
    m.columns[2].z = scale;
    m.columns[3].z = pivot.z * (1 - scale);
    return m;
  }
  if (!strncmp(name, "cone", 4) && (name[4] == 0 || name[5] == 'c')) {
    simd_float4x4 m = matrix_identity_float4x4;
    m.columns[3].y = s->cone * part->travel[1];
    return m;
  }
  if (!strcmp(name, "lid") ) return about_pivot(pivot, simd_quaternion(part->travel[1] * s->lid, axis), simd_make_float3(0, 0, 0));
  if (!strcmp(name, "needle")) return about_pivot(pivot, simd_quaternion(part->travel[0] + (part->travel[1] - part->travel[0]) * s->needle, axis), simd_make_float3(0, 0, 0));
  if (!strncmp(name, "cylinder", 8) && name[8] != '_') return about_pivot(pivot, simd_quaternion(s->cylinder_step * (float)M_PI / 8, axis), simd_make_float3(0, 0, 0));
  if (!strcmp(name, "cylinder_shaft")) return about_pivot(pivot, simd_quaternion(s->cylinder_step * (float)M_PI / 8, axis), simd_make_float3(0, 0, 0));
  if (!strncmp(name, "tine_", 5)) return about_pivot(pivot, simd_quaternion(s->tines[name[5] - '0'] * part->travel[1], axis), simd_make_float3(0, 0, 0));
  if (!strncmp(name, "cross", 5)) {
    // One rigid cross on a ball: pressing an arm rocks it all, and the far tip rises.
    float limit = part->travel[1];
    simd_quatf rock = simd_mul(simd_quaternion(-s->cross[0] * limit, simd_make_float3(0, 0, 1)), simd_quaternion(s->cross[1] * limit, simd_make_float3(1, 0, 0)));
    return about_pivot(pivot, rock, simd_make_float3(0, 0, 0));
  }
  if (!strncmp(name, "key_", 4)) { amount = name[4] == 'w' ? s->keys[0] : name[4] == 's' ? s->keys[1] : name[4] == 'e' ? s->keys[2] : s->keys[3]; turns = false; }
  else if (!strncmp(name, "pill_", 5)) { amount = s->pills[name[5] == 'r']; turns = false; }
  else if (!strcmp(name, "home")) { amount = s->home; turns = false; }
  else if (!strcmp(name, "mute")) { amount = s->mute; turns = false; }
  else if (!strncmp(name, "shoulder_", 9)) amount = s->shoulders[name[9] == 'r'];
  else if (!strncmp(name, "trigger_", 8)) amount = s->triggers[name[8] == 'r'];
  else if (!strncmp(name, "gear_", 5)) {
    float turn = s->gear_turn[name[5] == 'r'];
    return about_pivot(pivot, simd_quaternion(turn * part->travel[1], axis), simd_make_float3(0, 0, 0));
  } else return matrix_identity_float4x4;
  float value = part->travel[0] + (part->travel[1] - part->travel[0]) * amount;
  if (!turns) return about_pivot(pivot, none, axis * value);
  return about_pivot(pivot, simd_quaternion(value, axis), simd_make_float3(0, 0, 0));
}

// The shader's loop over the crew is the dearest thing in a frame, and most parts are nowhere near most of them. A part's box in its own
// frame, against each capsule's sphere.
static uint32_t crew_near_part(const hs_part *part, simd_float4x4 model, const hs_scene *scene) {
  uint32_t near = 0;
  simd_float3 lo = vec(part->bounds_min) - 0.002f, hi = vec(part->bounds_max) + 0.002f;
  simd_float3 axes[3] = {model.columns[0].xyz, model.columns[1].xyz, model.columns[2].xyz};
  for (int c = 0; c < scene->capsule_count; c++) {
    simd_float3 to = scene->capsules[c].reach.xyz - model.columns[3].xyz;
    simd_float3 local = simd_make_float3(simd_dot(to, axes[0]), simd_dot(to, axes[1]), simd_dot(to, axes[2]));
    simd_float3 past = simd_max(simd_max(lo - local, local - hi), simd_make_float3(0, 0, 0));
    if (simd_dot(past, past) < scene->capsules[c].reach.w) near |= 1u << c;
  }
  return near;
}

void hs_rig_compute(const hs_mesh_pack *pack, const hs_scene *scene, hs_draw *draws) {
  // A pack whose window is shallower than the hall carries a part named hall_fit: its pivot and travel say how the hall is pressed along z.
  // Each member moves as a whole, from the body's own place, so limbs stay on their bodies.
  int fit = hs_mesh_pack_find(pack, "hall_fit");
  float fit_center = fit >= 0 ? pack->parts[fit].pivot[2] : 0, fit_scale = fit >= 0 ? pack->parts[fit].travel[1] : 1;
  for (uint32_t i = 0; i < pack->part_count && i < HS_MAX_PART_POSES; i++) {
    const hs_part *part = &pack->parts[i];
    simd_float4x4 model;
    if (part->parent >= 0) {
      // A crew member's part: its own bone in the object's frame. The part's mesh is built around that bone, in millimetres
      // already scaled to metres by the exporter.
      simd_float4x4 bone = scene->crew_bones[part->parent / 16][part->parent % 16];
      if (fit >= 0) bone.columns[3].z += (scene->crew_bones[part->parent / 16][0].columns[3].z - fit_center) * (fit_scale - 1);
      model = simd_mul(scene->object, bone);
    } else {
      model = local_motion(part, scene);
      if (scene->part_extra) model = simd_mul(scene->part_extra[i], model);
      model = simd_mul(scene->object, model);
    }
    draws[i].model = model;
    draws[i].normal = simd_transpose(simd_inverse(model));
    draws[i].material = part->material;
    draws[i].near_crew = crew_near_part(part, model, scene);
    // The small keys travel too little to read from across the picture, so they also light from below while held.
    draws[i].pressed_light = 0;
    if (!strncmp(part->name, "pill_", 5)) draws[i].pressed_light = scene->pills[part->name[5] == 'r'] * 1.6f;
    else if (!strcmp(part->name, "mute")) draws[i].pressed_light = fmaxf(scene->mute * 1.6f, scene->mic_light ? 1.1f : 0);
    else if (!strcmp(part->name, "mic_lamp")) draws[i].pressed_light = scene->mic_light ? 6.0f : 0;
    else if (!strncmp(part->name, "player_light_", 13)) draws[i].pressed_light = (scene->player_lights >> (part->name[13] - '0')) & 1 ? 6.0f : 0;
    draws[i].glow = part->pass == HS_PASS_DOME ? 1.4f : part->pass == HS_PASS_CLEAR ? 0.35f : 1.0f;
  }
}
