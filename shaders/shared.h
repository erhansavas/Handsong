// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What the CPU and the GPU agree on. Included by both the app and the shaders.
#pragma once
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
typedef uint uint32_t;
typedef float4x4 hs_float4x4;
typedef float3 hs_float3;
typedef float4 hs_float4;
typedef float2 hs_float2;
#else
#include <simd/simd.h>
typedef simd_float4x4 hs_float4x4;
typedef simd_float3 hs_float3;
typedef simd_float4 hs_float4;
typedef simd_float2 hs_float2;
#endif

enum { HS_MAX_LIGHTS = 4, HS_MAX_CAPSULES = 24, HS_MATERIALS = 32 };

// The surface patterns a machined part can carry, each antialiased in the shader.
enum { HS_PATTERN_NONE, HS_PATTERN_BRUSHED, HS_PATTERN_PERLAGE, HS_PATTERN_SUNRAY, HS_PATTERN_COPPER_WIRE, HS_PATTERN_KNURL,
       HS_PATTERN_WATER, HS_PATTERN_WOOD, HS_PATTERN_GRAVEL, HS_PATTERN_LATTICE };

enum {
  HS_MAT_BRASS_BRUSHED, HS_MAT_BRASS_POLISHED, HS_MAT_STEEL_BLUE, HS_MAT_STEEL_BRUSHED, HS_MAT_RUBY, HS_MAT_CERAMIC, HS_MAT_COPPER,
  HS_MAT_BRASS_PERLAGE, HS_MAT_BRONZE_DARK, HS_MAT_RUBBER, HS_MAT_PAPER, HS_MAT_FILAMENT, HS_MAT_ACRYLIC, HS_MAT_LENS, HS_MAT_KNURLED, HS_MAT_WATER, HS_MAT_WOOD,
  HS_MAT_GRAVEL, HS_MAT_LATTICE, HS_MAT_LATTICE_NODE, HS_MAT_PINS, HS_MAT_COPPER_PLAIN, HS_MAT_STEEL_SATIN, HS_MAT_STEEL_POLISHED,
  // For other model packs, with parts of moulded plastic.
  HS_MAT_MATTE_DARK, HS_MAT_GLOSS_DARK, HS_MAT_SMOKED_KEY, HS_MAT_SLATE, HS_MAT_PRINT, HS_MAT_LAST,
};

typedef struct {
  hs_float4 base;        // linear Display P3, a is metalness
  float roughness, anisotropy, pattern, glow;
} hs_material;

typedef struct {
  hs_float3 position;    // or direction when radius is 0
  float radius;          // the size of the source, so a highlight has the right softness
  hs_float3 color;       // linear Display P3
  float range;
} hs_light;

typedef struct { hs_float4 a, b, reach; } hs_capsule;   // two ends in xyz, radius in a.w, strength in b.w; a sphere holding all it can touch

typedef struct {
  hs_float4x4 view_projection, view, projection, light_view_projection, inverse_view_projection;
  hs_float3 camera;
  float time;
  hs_float2 size, one_over_size;
  float env_yaw, exposure, env_strength, dim;
  hs_light lights[HS_MAX_LIGHTS];
  hs_capsule capsules[HS_MAX_CAPSULES];
  int light_count, capsule_count;
  float backdrop_glow, shadow_strength;
  hs_float3 horizon_low;
  float horizon_at;
  hs_float3 horizon_high;
  float filament;        // the filament's own light, 0 to many
  hs_float3 filament_color;
  hs_float3 touch_color;
  float dome_flex;       // pressed dome, millimetres
  hs_float4 touch[2];    // xz on the dome in metres, then strength and age
  hs_float4 sh[9];       // the environment's diffuse light, spherical harmonics
  hs_float4 ripple[4];   // on the water inlay: xz in metres, strength, age in seconds
  float lattice_wake;    // how awake the whole lattice is, 0 to 1
  float near_plane;
  float pad_frame;
  hs_float3 black_lift;  // what the panel's black is warmed by, in its encoded values
  float caption_shade;   // how much the foot of the picture is dimmed, so that words can always be read
} hs_frame;

typedef struct {
  hs_float4x4 model, normal;
  int material;
  uint32_t near_crew;        // one bit for each crew member standing close enough to shade this part
  float glow, pressed_light;   // pressed_light: a small key lit from below while it is held
} hs_draw;

// Particles live on the GPU. A spawn request writes `count` of them at the head of the kind's range.
enum { HS_MAX_SPAWNS = 16, HS_PARTICLE_FINE = 170000, HS_PARTICLE_SOFT = 6000, HS_PARTICLES = HS_PARTICLE_FINE + HS_PARTICLE_SOFT };
enum { HS_KIND_SAND, HS_KIND_EMBER, HS_KIND_DUST, HS_KIND_STEAM };

typedef struct {
  hs_float3 position;
  float life;
  hs_float3 velocity;
  float age;
  hs_float3 color;
  float size;           // metres
  float kind;
  float seed;
  float pad[2];
} hs_particle;

typedef struct {
  hs_float3 position;
  float spread;
  hs_float3 velocity;
  float speed_spread;
  hs_float3 color;
  float size;
  float life;
  float kind;
  int count;
  int first;            // the slot to start at, set by the renderer
} hs_particle_spawn;

typedef struct {
  hs_float4x4 object;
  hs_float3 gravity;    // in the object's frame, metres per second squared
  float dt;
  float time;
  float chladni_m, chladni_n, chladni_shake;
  hs_float3 plate_center;
  float plate_radius;
  hs_float3 plate_up;
  float sand_amount;
  float sand_rise;      // how far above the lid
  float pad[3];
} hs_particle_step;

// Mesh vertices: 32 bytes.
typedef struct {
#ifdef __METAL_VERSION__
  packed_float3 position;
  uint normal, tangent;     // each 10:10:10:2 signed
  packed_float2 uv;
  uint bake;                // occlusion, wall thickness in 10 mm, cavity, light, each 0 to 255
#else
  float position[3];
  uint32_t normal, tangent;
  float uv[2];
  uint32_t bake;
#endif
} hs_vertex;

// The finish pass's text: rendered from a distance field, crisp at the panel's own pixels.
typedef struct {
  hs_float4 position;    // x, y, w, h in pixels
  hs_float4 uv;          // u0, v0, u1, v1
  hs_float4 color;       // premultiplied by nothing: rgb, a
} hs_glyph;
