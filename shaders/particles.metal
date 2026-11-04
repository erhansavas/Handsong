// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What moves in the air: sand on the plate, embers, dust and steam, all on the GPU, and the pen that writes the name.
#include "lighting.h"

static float random01(uint seed) {
  seed = seed * 747796405u + 2891336453u;
  seed = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
  seed = (seed >> 22u) ^ seed;
  return float(seed) / 4294967295.0;
}

static float3 random_unit(uint seed) {
  float a = random01(seed) * 6.2831853, z = random01(seed + 1) * 2 - 1, r = sqrt(max(1 - z * z, 0.0));
  return float3(r * cos(a), z, r * sin(a));
}

kernel void particle_spawn(device hs_particle *particles [[buffer(0)]], constant hs_particle_spawn &spawn [[buffer(1)]], constant uint &first [[buffer(2)]],
                           constant uint &capacity_start [[buffer(3)]], constant uint &capacity [[buffer(4)]], constant uint &tick [[buffer(5)]], uint id [[thread_position_in_grid]]) {
  uint slot = capacity_start + (first + id) % capacity;
  uint seed = id * 4813u + tick * 7919u;
  device hs_particle &p = particles[slot];
  p.position = spawn.position + random_unit(seed) * spawn.spread * sqrt(random01(seed + 2));
  p.velocity = spawn.velocity + random_unit(seed + 3) * spawn.speed_spread * random01(seed + 4);
  p.color = spawn.color * (0.7 + 0.6 * random01(seed + 5));
  p.size = spawn.size * (0.7 + 0.6 * random01(seed + 6));
  p.life = spawn.life * (0.6 + 0.8 * random01(seed + 7));
  p.age = 0;
  p.kind = spawn.kind;
  p.seed = random01(seed + 8);
}

static float chladni(float2 q, float m, float n) {
  const float pi = 3.14159265;
  return cos(n * pi * q.x) * cos(m * pi * q.y) - cos(m * pi * q.x) * cos(n * pi * q.y);
}

kernel void particle_step(device hs_particle *particles [[buffer(0)]], constant hs_particle_step &s [[buffer(1)]], uint id [[thread_position_in_grid]]) {
  device hs_particle &p = particles[id];
  if (p.kind == HS_KIND_SAND) {
    // Each grain is shaken as long as the plate moves where it lies, so it wanders until it finds a line that stands still.
    float2 q = (p.position.xz - s.plate_center.xz) / s.plate_radius;
    const float e = 0.01;
    float psi = chladni(q, s.chladni_m, s.chladni_n);
    float2 gradient = float2(chladni(q + float2(e, 0), s.chladni_m, s.chladni_n) - chladni(q - float2(e, 0), s.chladni_m, s.chladni_n),
                             chladni(q + float2(0, e), s.chladni_m, s.chladni_n) - chladni(q - float2(0, e), s.chladni_m, s.chladni_n)) / (2 * e);
    float away = abs(psi) * s.chladni_shake;
    float2 jitter = float2(random01(uint(id * 13 + uint(s.time * 960))) - 0.5, random01(uint(id * 29 + uint(s.time * 960))) - 0.5);
    q += (-sign(psi) * gradient * 0.05 * s.chladni_shake + jitter * away * 1.1) * s.dt;
    float r = length(q);
    if (r > 0.97) q *= 0.97 / r;
    p.position.xz = q * s.plate_radius + s.plate_center.xz;
    float hop = away * 0.0009 * random01(uint(id * 53 + uint(s.time * 240)));
    p.position.y = s.plate_center.y + 0.00006 + hop;
    // The grains are only there as far as there is sand on the plate.
    p.life = p.seed < s.sand_amount ? 1000 : 0;
    p.age = 0;
    return;
  }
  if (p.age >= p.life) return;
  p.age += s.dt;
  float t = p.age / p.life;
  if (p.kind == HS_KIND_EMBER) {
    p.velocity += s.gravity * s.dt * 0.08;
    p.velocity *= 1 - 1.2 * s.dt;
  } else if (p.kind == HS_KIND_DUST) {
    p.velocity += s.gravity * s.dt * 0.04;
    p.velocity *= 1 - 2.4 * s.dt;
    float3 stir = float3(sin(p.position.z * 900 + s.time), sin(p.position.x * 700 - s.time * 0.7), cos(p.position.y * 800)) * 0.012;
    p.velocity += stir * s.dt * 8;
  } else {  // steam: rises, slows, spreads, curls
    p.velocity -= s.gravity * s.dt * 0.012;
    p.velocity *= 1 - 3.0 * s.dt;
    float3 curl = float3(sin(p.position.y * 900 + p.seed * 9), cos(p.position.z * 700 + s.time), sin(p.position.x * 800 - s.time)) * 0.02;
    p.velocity += curl * s.dt * 12 * t;
    p.size += s.dt * 0.0036;
  }
  p.position += p.velocity * s.dt;
}

struct ParticleOut {
  float4 position [[position]];
  float2 uv;
  float3 color;
  float view_z;
  float softness;
};

vertex ParticleOut particle_vertex(const device hs_particle *particles [[buffer(0)]], constant hs_frame &f [[buffer(2)]], constant hs_particle_step &s [[buffer(3)]],
                                   uint corner_id [[vertex_id]], uint instance [[instance_id]]) {
  const device hs_particle &p = particles[instance];
  ParticleOut out;
  float2 corner = float2(corner_id & 1, corner_id >> 1) * 2 - 1;
  out.uv = corner;
  if (p.age >= p.life || p.life <= 0) {
    out.position = float4(2, 2, 2, 1);
    out.color = 0;
    out.view_z = 0;
    out.softness = 0;
    return out;
  }
  float3 world = (s.object * float4(p.position, 1)).xyz;
  float4 view = f.view * float4(world, 1);
  float distance = max(-view.z, 0.001);
  float t = p.age / p.life;
  float fade = p.kind == HS_KIND_SAND ? 1 : smoothstep(0.0, 0.08, t) * (1 - t) * (1 - t);
  float pixel = distance * 2 * f.one_over_size.y * 0.7;   // a metre per pixel at that distance, near enough
  float size = max(p.size, pixel * 0.9);
  // A puff that passes the lens would fill the frame many times over. It thins out and is gone before it can, and costs nothing then.
  float screen_share = size * f.projection.columns[1].y / distance;
  float near = smoothstep(0.03, 0.06, distance) * (1 - smoothstep(0.18, 0.35, screen_share));
  if (near <= 0) {
    out.position = float4(2, 2, 2, 1);
    out.color = 0;
    out.view_z = 0;
    out.softness = 0;
    return out;
  }
  // A particle thinner than a pixel still gives the same light, spread over its pixel, so it never sparkles as it moves.
  float area = (p.size * p.size) / (size * size);
  float4 at = view + float4(corner * size, 0, 0);
  out.position = f.projection * at;
  out.color = p.color * fade * area * near;
  if (p.kind == HS_KIND_EMBER) out.color *= (1 - t * 0.7);
  out.view_z = distance;
  out.softness = max(size * 1.5, 0.0005);
  return out;
}

fragment float4 particle_fragment(ParticleOut in [[stage_in]], depth2d<float> depth [[texture(0)]], constant hs_frame &f [[buffer(2)]], constant float &depth_scale [[buffer(1)]]) {
  float r2 = dot(in.uv, in.uv);
  if (r2 > 1) return float4(0);
  // Soft against whatever stands behind it, so there is no hard line where a plume meets a part.
  float scene = depth.read(uint2(in.position.xy * depth_scale));
  float scene_distance = f.near_plane / max(scene, 1e-6);
  float soft = saturate((scene_distance - in.view_z) / in.softness);
  float shape = 1 - r2;
  float3 light = in.color * shape * shape * soft;
  // The half-resolution plume stays in plain light until it is laid over the scene; the fine light goes straight on.
  return float4(depth_scale > 1.5 ? light : compress_light(light), 0);
}

fragment float4 sand_fragment(ParticleOut in [[stage_in]]) {
  float r2 = dot(in.uv, in.uv);
  float shape = saturate(1 - r2);
  return float4(compress_light(in.color * shape * shape), 0);
}

struct PenOut {
  float4 position [[position]];
  float across;
  float heat;
  float view_z;
};

vertex PenOut pen_vertex(const device float4 *points [[buffer(0)]], constant hs_frame &f [[buffer(2)]], constant hs_particle_step &s [[buffer(3)]], constant float2 &pen [[buffer(1)]],
                         uint corner_id [[vertex_id]], uint instance [[instance_id]]) {
  float4 a = points[instance], b = points[instance + 1];
  PenOut out;
  out.across = float(corner_id >> 1) * 2 - 1;
  float side = float(corner_id & 1);
  if (a.w < 0 || b.w < 0 || a.w > pen.x) {
    out.position = float4(2, 2, 2, 1);
    out.heat = 0;
    out.view_z = 0;
    return out;
  }
  // The stroke ends exactly where the pen is, inside its segment.
  float reach = saturate((pen.x - a.w) / max(b.w - a.w, 1e-6));
  side *= reach;
  float along = mix(a.w, b.w, side);
  float3 wa = (s.object * float4(a.xyz, 1)).xyz, wb = (s.object * float4(b.xyz, 1)).xyz;
  float4 va = f.view * float4(wa, 1), vb = f.view * float4(wb, 1);
  float2 direction = normalize((vb - va).xy + float2(1e-6, 0));
  float2 normal = float2(-direction.y, direction.x);
  float4 at = mix(va, vb, side);
  float distance = max(-at.z, 0.001);
  float width = max(0.00034, distance * 2 * f.one_over_size.y * 1.4);
  at.xy += normal * out.across * width;
  out.position = f.projection * at;
  float since = max(pen.x - along, 0.0);
  out.heat = pen.y * (0.8 + 6.0 * exp(-since * 45.0));
  out.view_z = distance;
  return out;
}

fragment float4 pen_fragment(PenOut in [[stage_in]], depth2d<float> depth [[texture(0)]], constant hs_frame &f [[buffer(2)]]) {
  float scene = depth.read(uint2(in.position.xy));
  float scene_distance = f.near_plane / max(scene, 1e-6);
  float soft = saturate((scene_distance - in.view_z) / 0.002);
  float core = exp(-in.across * in.across * 5.0);
  float3 color = float3(1.0, 0.46, 0.18) * 3.2 * in.heat * core;
  color += float3(1.0, 0.85, 0.65) * 2.4 * in.heat * exp(-in.across * in.across * 22.0);
  return float4(compress_light(color * soft), 0);
}

// The half-resolution plume, laid over the scene.
struct ScreenOut {
  float4 position [[position]];
  float2 uv;
};

fragment float4 soft_composite(ScreenOut in [[stage_in]], texture2d<float> soft [[texture(0)]], sampler s [[sampler(0)]]) {
  return float4(compress_light(soft.sample(s, in.uv).rgb), 0);
}
