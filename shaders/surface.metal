// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Every opaque part: metals, jewels, ceramic, rubber. All of it antialiased in the shader, with no discard anywhere.
#include "lighting.h"

struct SurfaceIn {
  float4 position [[position]];
  float3 world;
  float3 normal, tangent;
  float3 local;
  float2 uv;
  float4 bake;
};

// Circular graining, as a watchmaker leaves it on a plate: overlapping spots of concentric marks, seen as a soft shimmer.
// Beyond a pixel's reach it fades into plain brushed metal instead of aliasing.
static float2 perlage(float2 p, float footprint, thread float &strength) {
  const float cell = 0.0015;
  float2 q = p / cell;
  float2 row = float2(q.x, q.y + 0.5 * floor(q.x));
  float2 id = floor(row);
  float2 f = row - id - 0.5;
  strength = 1 - smoothstep(cell * 0.15, cell * 0.45, footprint);
  float r = length(f);
  float2 dir = f / max(r, 1e-4);
  return dir * sin(r * 34.0) * 0.5 * smoothstep(0.5, 0.42, r);
}

static float sample_shadow(depth2d<float> map, sampler shadow_sampler, float4 clip, float bias) {
  float3 q = clip.xyz / clip.w;
  float2 uv = q.xy * float2(0.5, -0.5) + 0.5;
  if (any(uv < 0.0) || any(uv > 1.0)) return 1;
  // Two filtered taps: each already compares four texels, and the soft key light needs no more.
  const float2 disc[2] = {float2(-0.71, -0.33), float2(0.71, 0.33)};
  float sum = 0;
  for (int i = 0; i < 2; i++) sum += map.sample_compare(shadow_sampler, uv + disc[i] * 2.2 / 2048.0, q.z + bias);
  return sum / 2;
}

static float4 shade_surface(SurfaceIn in, constant hs_frame &f, constant hs_draw &draw, constant hs_material *materials, texturecube<float> env,
                            depth2d<float> shadow, sampler linear_sampler, sampler shadow_sampler) {
  constant hs_material &m = materials[draw.material];
  float3 n = normalize(in.normal), v = normalize(f.camera - in.world);
  float3 t = normalize(in.tangent - n * dot(in.tangent, n));
  float3 b = cross(n, t);
  float pattern = m.pattern;
  float footprint = length(fwidth(in.local));
  float alpha = max(m.roughness * m.roughness, 0.002);
  float anisotropy = m.anisotropy;
  float wood_tone = 1, wake = 0;

  if (pattern == HS_PATTERN_PERLAGE) {
    float strength;
    float2 tilt = perlage(in.local.xz, footprint, strength);
    n = normalize(n + (t * tilt.x + b * tilt.y) * 0.06 * strength);
    alpha = sqrt(alpha * alpha + (1 - strength) * 0.012);
  } else if (pattern == HS_PATTERN_COPPER_WIRE) {
    // Turns of enamelled wire, each a tiny rounded ridge, faded before they can alias.
    float turns = in.local.z / 0.00035;        // the bobbin lies along z, and the wire goes round it
    float fade = 1 - smoothstep(0.22, 0.5, fwidth(turns));     // kept until a turn is too fine for the pixels to show it, and not a moment longer
    float3 along = (draw.normal * float4(0, 0, 1, 0)).xyz;
    along = normalize(along - n * dot(along, n) + 1e-5);
    n = normalize(n + along * sin(turns * 2 * PI) * 0.35 * fade);
    // The ridges no longer drawn still scatter the light: what is averaged away in the normal comes back as roughness (a ridge of slope 0.35 sin, variance 0.35^2 / 2).
    alpha = sqrt(alpha * alpha + (1 - fade) * 0.061);
  } else if (pattern == HS_PATTERN_KNURL) {
    // A straight knurl on a turned part, in the bake's third channel. It is a normal pattern and not geometry so that it can
    // give way to its average roughness as soon as a tooth gets smaller than a few pixels, instead of sparkling.
    const float teeth = 72.0;
    float angle = in.uv.x * 2 * PI;
    float tooth_pixels = (2 * PI / teeth) / max(fwidth(in.uv.x) * 2 * PI, 1e-5);
    float fade = smoothstep(2.5, 6.0, tooth_pixels) * in.bake.z;
    float3 around = normalize(float3(-sin(angle), 0, cos(angle)));
    n = normalize(n + (draw.normal * float4(around, 0)).xyz * sin(angle * teeth) * 0.55 * fade);
    alpha = sqrt(alpha * alpha + in.bake.z * (1 - fade) * 0.09);
  } else if (pattern == HS_PATTERN_WATER) {
    // Slow standing swell, and rings from whatever disturbs it.
    float2 q = in.local.xz;
    float2 slope = float2(cos(q.x * 900 + f.time * 1.1) + cos((q.x + q.y) * 620 - f.time * 0.8), cos(q.y * 780 - f.time * 0.9) + cos((q.x - q.y) * 520 + f.time * 1.3)) * 0.012;
    for (int i = 0; i < 4; i++) {
      float2 d = q - f.ripple[i].xy;
      float r = length(d);
      float age = f.ripple[i].w;
      float front = age * 0.030;
      if (f.ripple[i].z <= 0 || age > 3.0 || abs(r - front) > 0.008) continue;     // past this it is far below what a pixel could show
      float ring = exp(-pow((r - front) / 0.0022, 2.0)) * f.ripple[i].z * exp(-age * 1.8);
      slope += d / max(r, 1e-5) * sin((r - front) * 3200.0) * ring * 0.35;
    }
    float fade = 1 - smoothstep(0.0003, 0.0009, footprint);
    n = normalize(n + float3(slope.x, 0, slope.y) * fade);
  } else if (pattern == HS_PATTERN_WOOD) {
    // Rings of a tangentially sawn plank, plus fine straight pores that give way to plain colour before they can alias.
    float2 q = in.local.xz * 1000.0;
    float warp = sin(q.x * 0.09 + q.y * 0.03) * 1.6 + sin(q.y * 0.11 - q.x * 0.05) * 0.9;
    float ring = 0.5 + 0.5 * sin((q.y * 0.9 + warp) * 2.4);
    wood_tone = mix(0.62, 1.25, ring * ring);
    float pores = sin(q.x * 11.0 + sin(q.y * 1.3) * 2.0);
    float fade = 1 - smoothstep(0.00004, 0.00014, footprint);
    wood_tone *= 1 - 0.14 * fade * smoothstep(0.6, 1.0, pores);
  } else if (pattern == HS_PATTERN_GRAVEL) {
    // Small rounded stones, each lit as its own dome: the eye reads rough ground long before a pixel could shimmer on it.
    const float cell = 0.00125;
    float2 q = in.local.xz / cell;
    float2 id = floor(q - 0.5);
    float nearest = 9;
    float2 best = 0;
    float2 best_id = id;
    for (int y = 0; y <= 1; y++) {
      for (int x = 0; x <= 1; x++) {
        float2 c = id + float2(x, y);
        float2 jitter = fract(sin(float2(dot(c, float2(127.1, 311.7)), dot(c, float2(269.5, 183.3)))) * 43758.5453);
        float2 to = c + jitter * 0.5 + 0.25 + 0.0 - q;
        float d = dot(to, to);
        if (d < nearest) { nearest = d; best = to; best_id = c; }
      }
    }
    float fade = 1 - smoothstep(cell * 0.2, cell * 0.6, footprint);
    float shade = fract(sin(dot(best_id, float2(41.3, 289.1))) * 19783.5);
    wood_tone = mix(1.0, 0.55 + shade * 1.0, fade) * mix(1.0, 1 - smoothstep(0.15, 0.55, nearest) * 0.8, fade);
    n = normalize(n - float3(best.x, 0, best.y) * 0.9 * fade);
  } else if (pattern == HS_PATTERN_LATTICE) {
    // The wire wakes around a finger, and a pulse runs out from where it landed.
    for (int i = 0; i < 2; i++) {
      float2 d = in.local.xz - f.touch[i].xy;
      float r = length(d);
      float near_finger = exp(-r * r / (0.0052 * 0.0052)) * f.touch[i].z;
      float front = f.touch[i].w * 0.055;
      float pulse = exp(-pow((r - front) / 0.0035, 2.0)) * exp(-f.touch[i].w * 2.4) * step(0.0, f.touch[i].w);
      wake += near_finger + pulse;
    }
    wake += f.lattice_wake * 0.10;
  } else if (pattern == HS_PATTERN_SUNRAY) {
    float angle = atan2(in.local.z, in.local.x);
    float fade = 1 - smoothstep(0.0001, 0.0004, footprint);
    t = normalize(float3(-sin(angle), 0, cos(angle)));
    b = cross(n, t);
    anisotropy = m.anisotropy * (0.6 + 0.4 * fade);
  }

  alpha = antialiased_alpha(alpha, n);
  float at = alpha * (1 + anisotropy), ab = max(alpha * (1 - anisotropy), 0.002);
  // The plate's triangles are far apart in its middle and close at its edges: a bake stored per vertex would blotch it, so a flat plate takes one even value.
  float baked = pattern == HS_PATTERN_PERLAGE ? 0.86 : in.bake.x;
  float occlusion = baked * capsule_occlusion(f, draw.near_crew, in.world, n);
  float3 base = m.base.rgb * wood_tone;
  float metal = m.base.a;
  float3 f0 = mix(float3(0.04), base, metal);
  float3 diffuse_color = base * (1 - metal);
  float nv = max(dot(n, v), 1e-3);

  float4 light_clip = f.light_view_projection * float4(in.world + n * 0.0006, 1);
  float shadowed = mix(1.0, sample_shadow(shadow, shadow_sampler, light_clip, 0.0004), f.shadow_strength);

  float3 color = 0;
  for (int i = 0; i < f.light_count; i++) {
    constant hs_light &light = f.lights[i];
    if (light.color.x + light.color.y + light.color.z <= 0) continue;
    float3 l;
    float falloff = 1;
    float a_light = 0;
    if (light.range > 0) {
      float3 to = light.position - in.world;
      float d2 = dot(to, to);
      if (d2 >= light.range * light.range) continue;      // a small light in a large room: most of the picture is out of its reach
      float d = sqrt(d2);
      l = to / max(d, 1e-5);
      float reach = saturate(1 - pow(d / light.range, 4.0));
      falloff = reach * reach / max(d2, 1e-5) * 1e-3;
      a_light = saturate(light.radius / max(d, 1e-4) * 0.5);
    } else {
      l = normalize(light.position);
      a_light = light.radius;
    }
    float nl = saturate(dot(n, l));
    if (nl <= 0) continue;
    float3 h = normalize(l + v);
    float nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    float at2 = saturate(at + a_light), ab2 = saturate(ab + a_light);
    float d = ggx_d_aniso(nh, dot(t, h), dot(b, h), at2, ab2);
    float vis = smith_v(nl, nv, (at2 + ab2) * 0.5);
    float3 spec = d * vis * fresnel(f0, vh) * (at * ab) / max(at2 * ab2, 1e-5);
    color += (diffuse_color / PI + spec) * light.color * falloff * nl * (i == 0 ? shadowed : 1.0);
  }

  // The stage's light: its reflection comes from an offset mirror direction for brushed metal, and the wide lobes from mips.
  float3 reflected = reflect(-v, n);
  if (anisotropy > 0.01) {
    float3 bent = cross(t, v);
    bent = normalize(cross(bent, t));
    reflected = reflect(-v, normalize(mix(n, bent, anisotropy * 0.6)));
  }
  float roughness_for_env = sqrt(max(at, ab));
  float3 env_light = env.sample(linear_sampler, rotate_env(reflected, f.env_yaw), level(roughness_for_env * 7.0)).rgb * f.env_strength;
  float specular_occlusion = saturate(pow(nv + occlusion, exp2(-16.0 * alpha - 1.0)) - 1.0 + occlusion);
  color += env_light * env_response(f0, roughness_for_env, nv) * specular_occlusion;
  color += diffuse_color * diffuse_sh(f, n) * occlusion * f.env_strength;
  color += base * m.glow * draw.glow * (pattern == HS_PATTERN_LATTICE ? 0.0 : 1.0);
  // What senses glows green, the one thing here that does: on brass, in warm light, nothing else could be read at a glance.
  // Under the line the surface steps back, so it stays readable over the brightest metal.
  if (pattern == HS_PATTERN_LATTICE) {
    color *= 1.0 - saturate(wake) * 0.55;
    color += float3(f.touch_color) * wake * (m.glow > 0 ? 7.0 : 1.6);
  }
  color += float3(1.0, 0.55, 0.25) * draw.pressed_light;
  return float4(compress_light(color), 1);
}

fragment float4 surface_fragment(SurfaceIn in [[stage_in]], constant hs_frame &f [[buffer(2)]], constant hs_draw &draw [[buffer(1)]],
                                 constant hs_material *materials [[buffer(3)]], texturecube<float> env [[texture(0)]],
                                 depth2d<float> shadow [[texture(1)]], sampler linear_sampler [[sampler(0)]], sampler shadow_sampler [[sampler(1)]]) {
  return shade_surface(in, f, draw, materials, env, shadow, linear_sampler, shadow_sampler);
}

// The back wall of the clear body, seen from inside: only what it reflects, added to what lies behind it.
fragment float4 clear_back_fragment(SurfaceIn in [[stage_in]], constant hs_frame &f [[buffer(2)]], texturecube<float> env [[texture(0)]],
                                    sampler linear_sampler [[sampler(0)]]) {
  float3 n = -normalize(in.normal), v = normalize(f.camera - in.world);
  float nv = saturate(dot(n, v));
  // Only the far wall's slanted parts show, and softly: clear plastic gives the eye its edges, not a mirror.
  float internal = 0.008 + 0.14 * pow(1 - nv, 5.0);
  float3 r = reflect(-v, n);
  float3 color = env.sample(linear_sampler, rotate_env(r, f.env_yaw), level(2.2)).rgb * internal * f.env_strength;
  return float4(compress_light(color), 0);
}
