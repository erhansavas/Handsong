// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Light shared by the solid and clear materials.
#pragma once
#include "shared.h"

constant float PI = 3.14159265;

// Everything is written to the multisampled targets through this reversible curve, so that the four samples of a pixel average in
// something close to what the eye sees. Averaged in plain light, one bright sample would own the pixel and an edge against a
// highlight would stay jagged however many samples it had.
static inline float3 compress_light(float3 c) { return c / (0.4 + max(c.r, max(c.g, c.b))); }
static inline float3 expand_light(float3 c) { return c * 0.4 / max(1.0 - max(c.r, max(c.g, c.b)), 0.015); }

static inline float3 rotate_env(float3 d, float yaw) {
  float c = cos(yaw), s = sin(yaw);
  return float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

static inline float3 diffuse_sh(constant hs_frame &f, float3 n) {
  n = rotate_env(n, f.env_yaw);
  float x = n.x, y = n.y, z = n.z;
  float3 c = f.sh[0].rgb * 0.282095;
  c += (f.sh[1].rgb * y + f.sh[2].rgb * z + f.sh[3].rgb * x) * 0.488603;
  c += (f.sh[4].rgb * x * y + f.sh[5].rgb * y * z + f.sh[7].rgb * x * z) * 0.474943;
  c += f.sh[6].rgb * (3 * z * z - 1) * 0.158 + f.sh[8].rgb * (x * x - y * y) * 0.274;
  return max(c, 0.0);
}

// Environment lighting response, in the analytic fit by Lazarov.
static inline float3 env_response(float3 f0, float roughness, float nv) {
  float4 r = roughness * float4(-1, -0.0275, -0.572, 0.022) + float4(1, 0.0425, 1.04, -0.04);
  float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
  return f0 * ab.x + ab.y;
}

// Roughness raised by how fast the normal turns inside one pixel, so a curved highlight never sparkles under sampling
// (Tokuyoshi and Kaplanyan). It works for every light and for the environment alike.
static inline float antialiased_alpha(float alpha, float3 n) {
  float3 du = dfdx(n), dv = dfdy(n);
  const float sigma2 = 0.2 / (2.0 * PI), kappa = 0.04;
  float variance = sigma2 * (dot(du, du) + dot(dv, dv));
  return sqrt(alpha * alpha + min(2.0 * variance, kappa));
}

static inline float ggx_d(float nh, float a2) {
  float d = nh * nh * (a2 - 1) + 1;
  return a2 / (PI * d * d);
}

static inline float ggx_d_aniso(float nh, float th, float bh, float at, float ab) {
  float d = th * th / (at * at) + bh * bh / (ab * ab) + nh * nh;
  return 1.0 / (PI * at * ab * d * d);
}

static inline float smith_v(float nl, float nv, float a) {
  float k = a * 0.5;
  return 0.25 / max((nl * (1 - k) + k) * (nv * (1 - k) + k), 1e-4);
}

static inline float3 fresnel(float3 f0, float vh) {
  return f0 + (1 - f0) * pow(1 - vh, 5.0);
}

// A capsule's soft occlusion of its surroundings, so a figure sits on what it stands on.
static inline float capsule_occlusion(constant hs_frame &f, uint near, float3 p, float3 n) {
  float seen = 1;
  while (near != 0) {
    int i = ctz(near);
    near &= near - 1;
    float3 from_middle = p - f.capsules[i].reach.xyz;
    if (dot(from_middle, from_middle) > f.capsules[i].reach.w) continue;
    float3 a = f.capsules[i].a.xyz, b = f.capsules[i].b.xyz;
    float radius = f.capsules[i].a.w;
    float3 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-9));
    float3 to = a + ab * t - p;
    float distance2 = dot(to, to);
    float reach = radius * 3.2;
    if (distance2 > reach * reach) continue;
    float d = sqrt(distance2);
    float facing = saturate(dot(n, to / max(d, 1e-5)) * 0.6 + 0.4);
    float closeness = saturate(1 - (d - radius) / (reach - radius));
    seen *= 1 - f.capsules[i].b.w * closeness * closeness * facing * 0.85;
  }
  return seen;
}
