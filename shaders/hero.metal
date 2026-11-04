// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The clear body and dome, drawn over the finished interior: it bends what lies behind it, tints it by how much plastic the
// light crossed, and shows the studio on its surface. Four samples per pixel keep its edges clean, nothing temporal.
#include "lighting.h"

struct SurfaceIn {
  float4 position [[position]];
  float3 world;
  float3 normal, tangent;
  float3 local;
  float2 uv;
  float4 bake;
};

struct ScreenOut {
  float4 position [[position]];
  float2 uv;
};

fragment float4 copy_fragment(ScreenOut in [[stage_in]], texture2d<float> scene [[texture(0)]]) {
  return scene.read(uint2(in.position.xy));
}

// What lies behind the wall is seen through a shift of a fraction of a pixel, and plain bilinear filtering would soften everything
// under the clear body. Catmull-Rom in five taps keeps the interior as sharp as the parts outside it.
static float3 sample_sharp(texture2d<float> scene, sampler s, float2 uv, float2 pixel) {
  float2 at = uv / pixel, whole = floor(at - 0.5) + 0.5, f = at - whole;
  float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f), w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
  float2 w12 = w1 + w2, middle = (whole + w2 / w12) * pixel, before = (whole - 1.0) * pixel, after = (whole + 2.0) * pixel;
  float3 c = scene.sample(s, float2(middle.x, before.y), level(0)).rgb * w12.x * w0.y;
  c += scene.sample(s, float2(before.x, middle.y), level(0)).rgb * w0.x * w12.y;
  c += scene.sample(s, middle, level(0)).rgb * w12.x * w12.y;
  c += scene.sample(s, float2(after.x, middle.y), level(0)).rgb * w3.x * w12.y;
  c += scene.sample(s, float2(middle.x, after.y), level(0)).rgb * w12.x * w3.y;
  float total = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
  return max(c / total, 0.0);
}

fragment float4 clear_fragment(SurfaceIn in [[stage_in]], constant hs_frame &f [[buffer(2)]], constant hs_draw &draw [[buffer(1)]],
                               texture2d<float> scene [[texture(0)]], texturecube<float> env [[texture(1)]], sampler linear_sampler [[sampler(0)]]) {
  float3 n = normalize(in.normal), v = normalize(f.camera - in.world);
  // Toward the silhouette this changes faster than a pixel can show, so it is taken as its average over the pixel.
  float nv_sharp = saturate(dot(n, v));
  float nv = saturate(nv_sharp + fwidth(nv_sharp) * 0.35);
  float wall = in.bake.y * 0.010;                       // metres of plastic, baked
  float view_distance = length(f.camera - in.world);

  // Where the view ray leaves the wall, moved in screen space: enough to show the wall's strength, and a little dispersion.
  float3 inside = refract(-v, n, 1.0 / 1.49);
  float3 exit = in.world + inside * wall;
  float4 clip = f.view_projection * float4(exit, 1);
  float2 at = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
  float2 here = in.position.xy * f.one_over_size;
  float2 offset = at - here;
  float lens = draw.glow;   // the dome bends more than a flat wall
  offset += (n.xy * float2(1, -1)) * lens * 0.018 * (0.2 + (1 - nv)) * (0.22 / max(view_distance, 0.05));
  offset = clamp(offset, float2(-0.08), float2(0.08));
  float3 through;
  through = expand_light(min(sample_sharp(scene, linear_sampler, here + offset, f.one_over_size), float3(0.97)));
  // A slight dispersion, only where the view crosses the wall on a slant, as a difference from the sharp sample.
  if (nv < 0.55) {
    float3 g = expand_light(scene.sample(linear_sampler, here + offset, level(0)).rgb);
    through.r += expand_light(scene.sample(linear_sampler, here + offset * 0.985, level(0)).rgb).r - g.r;
    through.b += expand_light(scene.sample(linear_sampler, here + offset * 1.015, level(0)).rgb).b - g.b;
    through = max(through, 0.0);
  }

  // Beer's law through the wall, longer on a slant. Faint and slightly warm: smoked by distance, never tinted like a toy.
  float path = wall / max(sqrt(1.0 - (1.0 - nv * nv) / (1.49 * 1.49)), 0.25);
  float3 absorption = exp(-float3(16.0, 15.0, 15.5) * path);
  through *= absorption;

  float alpha = antialiased_alpha(0.012, n);
  float roughness = sqrt(alpha);
  float fresnel_value = 0.0387 + (1 - 0.0387) * pow(1 - nv, 5.0);
  float3 r = reflect(-v, n);
  float3 reflected = env.sample(linear_sampler, rotate_env(r, f.env_yaw), level(roughness * 7.0)).rgb * f.env_strength;
  reflected = min(reflected, float3(9.0));

  float3 lights = 0;
  for (int i = 0; i < f.light_count; i++) {
    constant hs_light &light = f.lights[i];
    if (light.color.x + light.color.y + light.color.z <= 0) continue;
    float3 l;
    float falloff = 1, a_light;
    if (light.range > 0) {
      float3 to = light.position - in.world;
      float d2 = dot(to, to);
      if (d2 >= light.range * light.range) continue;
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
    if (nh < 0.9) continue;      // a polished surface: past this its highlight is far below what any light here can show
    float a2 = alpha + a_light;
    float d = ggx_d(nh, a2 * a2);
    float vis = smith_v(nl, nv, a2);
    float fr = 0.0387 + (1 - 0.0387) * pow(1 - vh, 5.0);
    lights += d * vis * fr * (alpha * alpha) / max(a2 * a2, 1e-5) * light.color * falloff * nl;
  }

  // A clear edge gathers light from inside and glows faintly where the wall turns away: the look of cast acrylic.
  float edge = pow(1 - nv, 4.0) * saturate(wall / 0.004);
  float3 edge_glow = 0;
  if (edge > 0.002) edge_glow = diffuse_sh(f, -n) * edge * 0.30 * f.env_strength + f.filament_color * f.filament * edge * in.bake.w * 0.08;

  float3 color = through * (1 - fresnel_value) + reflected * fresnel_value + lights + edge_glow;
  return float4(compress_light(color), 1);
}

// Bloom, down and up, with wide tents: light that spills, without a threshold that pops.
kernel void bloom_down(texture2d<float> from [[texture(0)]], texture2d<float, access::write> to [[texture(1)]], sampler s [[sampler(0)]],
                       constant int &from_screen [[buffer(0)]], uint2 id [[thread_position_in_grid]]) {
  float2 pixel = 1.0 / float2(to.get_width(), to.get_height());
  float2 uv = (float2(id) + 0.5) * pixel;
  float2 t = pixel * 0.5;
  float3 a = from.sample(s, uv, level(0)).rgb, b = from.sample(s, uv + t, level(0)).rgb, c2 = from.sample(s, uv - t, level(0)).rgb;
  float3 d = from.sample(s, uv + float2(t.x, -t.y), level(0)).rgb, e = from.sample(s, uv + float2(-t.x, t.y), level(0)).rgb;
  if (from_screen) { a = expand_light(a); b = expand_light(b); c2 = expand_light(c2); d = expand_light(d); e = expand_light(e); }
  float3 c = a * 4.0 + b + c2 + d + e;
  to.write(float4(min(c / 8.0, float3(64.0)), 1), id);
}

kernel void bloom_up(texture2d<float> smaller [[texture(0)]], texture2d<float> same [[texture(1)]], texture2d<float, access::write> to [[texture(2)]],
                     sampler s [[sampler(0)]], uint2 id [[thread_position_in_grid]]) {
  float2 pixel = 1.0 / float2(to.get_width(), to.get_height());
  float2 uv = (float2(id) + 0.5) * pixel;
  float2 t = pixel;
  float3 c = smaller.sample(s, uv + float2(-t.x, 0), level(0)).rgb + smaller.sample(s, uv + float2(t.x, 0), level(0)).rgb +
             smaller.sample(s, uv + float2(0, -t.y), level(0)).rgb + smaller.sample(s, uv + float2(0, t.y), level(0)).rgb;
  c = c * 0.25 * 0.85 + same.read(id).rgb * 0.65;
  to.write(float4(c, 1), id);
}

struct FinishOut { float4 color [[color(0)]]; };

// Scene light to the panel: an authored curve from a 3D table, then fine noise of a fixed blue pattern that never moves,
// so dark gradients do not band and nothing shimmers.
fragment FinishOut finish_fragment(ScreenOut in [[stage_in]], constant hs_frame &f [[buffer(2)]], texture2d<float> hdr [[texture(0)]],
                                   texture2d<float> bloom [[texture(1)]], texture3d<float> grade [[texture(2)]],
                                   texture2d<float> noise [[texture(3)]], sampler s [[sampler(0)]]) {
  uint2 at = uint2(in.position.xy);
  // What four samples could not settle: a pixel standing out from a strong local edge is moved partway across that edge. Flat and softly shaded areas are never touched.
  float3 middle = hdr.read(at).rgb;
  float3 luma_weights = float3(0.25, 0.6, 0.15);
  uint2 last = uint2(hdr.get_width() - 1, hdr.get_height() - 1);
  float m = dot(middle, luma_weights);
  float south = dot(hdr.read(uint2(at.x, min(at.y + 1, last.y))).rgb, luma_weights), east = dot(hdr.read(uint2(min(at.x + 1, last.x), at.y)).rgb, luma_weights);
  // Most of a dark picture is flat: two looks settle it, and only what might be an edge pays for the rest.
  if (abs(south - m) + abs(east - m) > 0.03) {
    float north = dot(hdr.read(uint2(at.x, at.y > 0 ? at.y - 1 : 0)).rgb, luma_weights), west = dot(hdr.read(uint2(at.x > 0 ? at.x - 1 : 0, at.y)).rgb, luma_weights);
    float lowest = min(m, min(min(north, south), min(west, east))), highest = max(m, max(max(north, south), max(west, east)));
    float range = highest - lowest;
    if (range > 0.06) {
      float apart = saturate(abs((north + south + west + east) * 0.25 - m) / range);
      float shift = smoothstep(0.0, 1.0, apart) * 0.5;
      bool across_rows = abs(north + south - 2 * m) >= abs(west + east - 2 * m);
      float2 step = across_rows ? float2(0, abs(south - m) >= abs(north - m) ? 1 : -1) : float2(abs(east - m) >= abs(west - m) ? 1 : -1, 0);
      middle = hdr.sample(s, in.uv + step * shift * f.one_over_size, level(0)).rgb;
    }
  }
  float3 color = expand_light(middle) + bloom.sample(s, in.uv, level(0)).rgb * 0.055;
  color *= f.exposure * (1 - f.dim);
  // Words have to be readable over anything, even the bright ring: the foot of the picture sinks into shadow while they are up, with no edge to it.
  float foot = smoothstep(0.70, 0.90, in.uv.y);
  color *= 1 - f.caption_shade * foot * foot * 0.88;
  // A log shaper in front of the table: 16 stops, as cook_grade.py wrote it.
  float3 shaped = saturate((log2(max(color, 1e-5)) + 11.0) / 16.0);
  float3 encoded = grade.sample(s, shaped * (32.0 / 33.0) + 0.5 / 33.0, level(0)).rgb;
  // A backlit panel never reaches black: it glows blue-grey, most of all toward its edges, and a dark picture shows that up. So the darkest
  // values are never left to the panel: they become a very dim warm room, stronger at the edges, which the blue glow disappears into.
  // Everything brighter than deep shadow is untouched.
  float2 from_centre = in.uv * 2.0 - 1.0;
  float darkness = saturate(1.0 - dot(encoded, float3(0.3, 0.6, 0.1)) * 18.0);
  encoded += f.black_lift * darkness * darkness * (1.0 + 0.6 * dot(from_centre, from_centre) * 0.5);
  // Triangular noise from two offsets of the tile, so its strength does not depend on the level it sits on.
  // Each 128-pixel block starts somewhere else in the tile, or the tile's own period would stand out as a faint regular pattern.
  uint2 block = at >> 7;
  uint2 shift = uint2(block.x * 73u + block.y * 41u, block.y * 59u + block.x * 29u);
  float dither = noise.read((at + shift) & uint2(127)).r + noise.read((at + shift + uint2(53, 89)) & uint2(127)).r - 1.0;
  FinishOut out;
  out.color = float4(encoded + dither / 1023.0, 1);
  return out;
}

struct GlyphOut {
  float4 position [[position]];
  float2 uv;
  float4 color;
};

vertex GlyphOut glyph_vertex(constant hs_glyph &g [[buffer(0)]], constant hs_frame &f [[buffer(2)]], uint id [[vertex_id]]) {
  float2 corner = float2(id & 1, id >> 1);
  GlyphOut out;
  float2 pixel = g.position.xy + corner * g.position.zw;
  out.position = float4(pixel * f.one_over_size * float2(2, -2) + float2(-1, 1), 0, 1);
  out.uv = mix(g.uv.xy, g.uv.zw, corner);
  out.color = g.color;
  return out;
}

fragment float4 glyph_fragment(GlyphOut in [[stage_in]], texture2d<float> atlas [[texture(0)]], sampler s [[sampler(0)]]) {
  float d = atlas.sample(s, in.uv).r;
  float width = fwidth(d) * 0.75;
  float a = smoothstep(0.5 - width, 0.5 + width, d) * in.color.a;
  // A soft dark halo from the same field, so a letter keeps its edge against whatever lies under it.
  float halo = smoothstep(0.22, 0.5, d) * 0.7 * in.color.a;
  return float4(in.color.rgb * a, a + (1 - a) * halo);
}
