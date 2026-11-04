// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Vertex shaders and the backdrop.
#include "lighting.h"

struct SurfaceOut {
  float4 position [[position]];
  float3 world;
  float3 normal, tangent;
  float3 local;
  float2 uv;
  float4 bake;
};

static float3 unpack_snorm(uint v) {
  int3 raw = int3(v << 22, v << 12, v << 2) >> 22;
  return float3(raw) / 511.0;
}

vertex SurfaceOut surface_vertex(const device hs_vertex *vertices [[buffer(0)]], constant hs_draw &draw [[buffer(1)]],
                                 constant hs_frame &frame [[buffer(2)]], uint id [[vertex_id]]) {
  const device hs_vertex &v = vertices[id];
  SurfaceOut out;
  float4 world = draw.model * float4(float3(v.position), 1);
  out.world = world.xyz;
  out.local = float3(v.position);
  out.normal = (draw.normal * float4(unpack_snorm(v.normal), 0)).xyz;
  out.tangent = (draw.model * float4(unpack_snorm(v.tangent), 0)).xyz;
  out.uv = float2(v.uv);
  out.bake = unpack_unorm4x8_to_float(v.bake);
  out.position = frame.view_projection * world;
  return out;
}

vertex float4 shadow_vertex(const device hs_vertex *vertices [[buffer(0)]], constant hs_draw &draw [[buffer(1)]],
                            constant hs_frame &frame [[buffer(2)]], uint id [[vertex_id]]) {
  return frame.light_view_projection * draw.model * float4(float3(vertices[id].position), 1);
}

vertex float4 shadow_vertex_camera(const device hs_vertex *vertices [[buffer(0)]], constant hs_draw &draw [[buffer(1)]],
                                   constant hs_frame &frame [[buffer(2)]], uint id [[vertex_id]]) {
  return frame.view_projection * draw.model * float4(float3(vertices[id].position), 1);
}

struct ScreenOut {
  float4 position [[position]];
  float2 uv;
};

vertex ScreenOut screen_vertex(uint id [[vertex_id]]) {
  ScreenOut out;
  out.uv = float2((id << 1) & 2, id & 2);
  out.position = float4(out.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return out;
}

// The stage: a dark room with a warm glow low behind the object, so a clear thing has something to bend and an edge reads.
fragment float4 backdrop_fragment(ScreenOut in [[stage_in]], constant hs_frame &frame [[buffer(2)]], texture2d<float> noise [[texture(2)]]) {
  float2 centered = (in.uv - float2(0.5, frame.horizon_at)) * float2(frame.size.x / frame.size.y, 1.0);
  float glow = exp(-dot(centered, centered) * 4.2);
  float low = smoothstep(0.1, 1.0, in.uv.y);
  float3 color = float3(0.0028, 0.0024, 0.0030) + frame.horizon_low * glow * glow * frame.backdrop_glow + frame.horizon_high * glow * low * frame.backdrop_glow;
  // The working target's floats step by about one part in 64, and across a slow gradient those steps would line up into faint contours
  // at a regular spacing. A step's worth of the same blue noise breaks them up where they are made.
  uint2 pixel = uint2(in.position.xy);
  // Read sideways and mirrored, so that it shares none of its spectrum with the noise the last pass adds.
  float dither = noise.read(pixel.yx & uint2(127)).r + noise.read((uint2(127) - pixel.yx) & uint2(127)).r - 1.0;
  color *= 1.0 + dither / 64.0;
  return float4(compress_light(color), 1);
}
