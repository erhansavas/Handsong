// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include <os/lock.h>
#import "render/renderer.h"
#import "render/resources.h"
#import "render/rig.h"
#import "render/particles.h"
#import "render/pipeline_cache.h"
#import "render/text.h"
#include <stdatomic.h>

static const MTLPixelFormat kHdrFormat = MTLPixelFormatRG11B10Float;
static const MTLPixelFormat kDepthFormat = MTLPixelFormatDepth32Float;
static const MTLPixelFormat kScreenFormat = MTLPixelFormatBGR10A2Unorm;
static const float kNearPlane = 0.002f;
enum { kBloomLevels = 8, kBloomReach = 5, kShadowSize = 2048, kInFlight = 2, kPassCount = 5 };

static simd_float4x4 look_at(simd_float3 eye, simd_float3 target, simd_float3 up) {
  simd_float3 z = simd_normalize(eye - target), x = simd_normalize(simd_cross(up, z)), y = simd_cross(z, x);
  simd_float4x4 m = matrix_identity_float4x4;
  m.columns[0] = simd_make_float4(x.x, y.x, z.x, 0);
  m.columns[1] = simd_make_float4(x.y, y.y, z.y, 0);
  m.columns[2] = simd_make_float4(x.z, y.z, z.z, 0);
  m.columns[3] = simd_make_float4(-simd_dot(x, eye), -simd_dot(y, eye), -simd_dot(z, eye), 1);
  return m;
}

// Reversed depth, to infinity, for even precision at any distance.
static simd_float4x4 perspective(float fov_radians, float aspect, float near) {
  float f = 1 / tanf(fov_radians / 2);
  simd_float4x4 m = {0};
  m.columns[0].x = f / aspect;
  m.columns[1].y = f;
  m.columns[2].w = -1;
  m.columns[3].z = near;
  return m;
}

static simd_float4x4 orthographic(float half, float near, float far) {
  simd_float4x4 m = matrix_identity_float4x4;
  m.columns[0].x = 1 / half;
  m.columns[1].y = 1 / half;
  m.columns[2].z = 1 / (far - near);      // reversed: near maps to 1
  m.columns[3].z = far / (far - near);
  return m;
}

@implementation HSRenderer {
  id<MTLDevice> _device;
  id<MTLCommandQueue> _queue;
  const hs_mesh_pack *_pack;
  id<MTLBuffer> _vertices, _indices, _materials;
  HSResources *_resources;
  HSParticles *_particles;
  HSPipelineCache *_cache;
  HSTextCache *_text;
  id<MTLRenderPipelineState> _surface, _shadow, _clearBack, _backdrop, _copy, _clear, _finish, _glyph, _depthOnly;
  id<MTLComputePipelineState> _bloomDown, _bloomUp;
  id<MTLDepthStencilState> _depthWrite, _depthRead, _depthAlways, _depthNone;
  id<MTLSamplerState> _linear, _shadowCompare, _clampLinear;
  id<MTLTexture> _msColor, _msDepth, _msColor2, _msDepth2, _opaque, _opaqueDepth, _hdr, _shadowMap, _offscreen;
  id<MTLTexture> _down[kBloomLevels], _up[kBloomLevels];
  NSUInteger _width, _height;
  id<MTLBuffer> _drawBuffer[kInFlight];
  dispatch_semaphore_t _inFlight;
  unsigned _frameIndex;
  NSString *_shotPath;
  id<MTLCounterSampleBuffer> _timers;
  _Atomic double _lastGpu;
  double _passSum[kPassCount * 2];
  os_unfair_lock _passLock;
  long _passFrames;
  const uint8_t *_hidden;
  int _bloomFirst;
  bool _onlyOutside;
  float _cullMargin;
  bool _standsOutside[HS_MAX_PART_POSES];
  double _particlesUntil;
  simd_float4 _planes[5];
}

- (id<MTLRenderPipelineState>)pipeline:(id<MTLLibrary>)library vertex:(NSString *)vertex fragment:(NSString *)fragment
                                 color:(MTLPixelFormat)color depth:(MTLPixelFormat)depth samples:(NSUInteger)samples additive:(BOOL)additive {
  MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
  d.vertexFunction = [library newFunctionWithName:vertex];
  if (fragment) d.fragmentFunction = [library newFunctionWithName:fragment];
  d.rasterSampleCount = samples;
  d.depthAttachmentPixelFormat = depth;
  if (color != MTLPixelFormatInvalid) {
    d.colorAttachments[0].pixelFormat = color;
    if (additive) {
      d.colorAttachments[0].blendingEnabled = YES;
      d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
      d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    }
  }
  return [_cache render:d];
}

- (instancetype)initWithDevice:(id<MTLDevice>)device resources:(NSString *)resources meshPack:(const hs_mesh_pack *)pack {
  if (!(self = [super init])) return nil;
  _device = device;
  _queue = [device newCommandQueue];
  _pack = pack;
  _inFlight = dispatch_semaphore_create(kInFlight);
  NSError *error = nil;
  id<MTLLibrary> library = [device newLibraryWithURL:[NSURL fileURLWithPath:[resources stringByAppendingPathComponent:@"shaders.metallib"]] error:&error];
  if (!library) { NSLog(@"no shaders: %@", error); return nil; }
  _resources = [[HSResources alloc] initWithDevice:device queue:_queue folder:resources];
  if (!_resources) return nil;
  _vertices = [device newBufferWithBytes:pack->vertices length:pack->vertex_count * sizeof(hs_vertex) options:MTLResourceStorageModeShared];
  _indices = [device newBufferWithBytes:pack->indices length:pack->index_count * sizeof(uint32_t) options:MTLResourceStorageModeShared];
  _materials = [device newBufferWithBytes:hs_material_table() length:sizeof(hs_material) * HS_MATERIALS options:MTLResourceStorageModeShared];
  for (int i = 0; i < kInFlight; i++) _drawBuffer[i] = [device newBufferWithLength:sizeof(hs_draw) * HS_MAX_PART_POSES * 2 options:MTLResourceStorageModeShared];

  for (uint32_t i = 0; i < pack->part_count && i < HS_MAX_PART_POSES; i++) _standsOutside[i] = stands_outside(pack->parts[i].name);
  _cache = [[HSPipelineCache alloc] initWithDevice:device library:library];
  _surface = [self pipeline:library vertex:@"surface_vertex" fragment:@"surface_fragment" color:kHdrFormat depth:kDepthFormat samples:4 additive:NO];
  _clearBack = [self pipeline:library vertex:@"surface_vertex" fragment:@"clear_back_fragment" color:kHdrFormat depth:kDepthFormat samples:4 additive:YES];
  _backdrop = [self pipeline:library vertex:@"screen_vertex" fragment:@"backdrop_fragment" color:kHdrFormat depth:kDepthFormat samples:4 additive:NO];
  _shadow = [self pipeline:library vertex:@"shadow_vertex" fragment:nil color:MTLPixelFormatInvalid depth:kDepthFormat samples:1 additive:NO];
  _copy = [self pipeline:library vertex:@"screen_vertex" fragment:@"copy_fragment" color:kHdrFormat depth:kDepthFormat samples:4 additive:NO];
  _clear = [self pipeline:library vertex:@"surface_vertex" fragment:@"clear_fragment" color:kHdrFormat depth:kDepthFormat samples:4 additive:NO];
  {
    // The solid parts' depth again, at all four samples, so that the clear body's edge against any part is as clean as any other edge.
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [library newFunctionWithName:@"shadow_vertex_camera"];
    d.rasterSampleCount = 4;
    d.depthAttachmentPixelFormat = kDepthFormat;
    d.colorAttachments[0].pixelFormat = kHdrFormat;
    d.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
    _depthOnly = [_cache render:d];
  }
  _finish = [self pipeline:library vertex:@"screen_vertex" fragment:@"finish_fragment" color:kScreenFormat depth:MTLPixelFormatInvalid samples:1 additive:NO];
  _bloomDown = [_cache compute:@"bloom_down"];
  _bloomUp = [_cache compute:@"bloom_up"];
  if (!_surface || !_clearBack || !_backdrop || !_shadow || !_copy || !_clear || !_finish || !_bloomDown || !_bloomUp || !_depthOnly) return nil;

  MTLDepthStencilDescriptor *depth = [MTLDepthStencilDescriptor new];
  depth.depthCompareFunction = MTLCompareFunctionGreater;
  depth.depthWriteEnabled = YES;
  _depthWrite = [device newDepthStencilStateWithDescriptor:depth];
  depth.depthWriteEnabled = NO;
  _depthRead = [device newDepthStencilStateWithDescriptor:depth];
  depth.depthCompareFunction = MTLCompareFunctionAlways;
  depth.depthWriteEnabled = YES;
  _depthAlways = [device newDepthStencilStateWithDescriptor:depth];
  depth.depthCompareFunction = MTLCompareFunctionGreaterEqual;
  depth.depthWriteEnabled = NO;
  _depthNone = [device newDepthStencilStateWithDescriptor:depth];

  MTLSamplerDescriptor *s = [MTLSamplerDescriptor new];
  s.minFilter = s.magFilter = MTLSamplerMinMagFilterLinear;
  s.mipFilter = MTLSamplerMipFilterLinear;
  s.maxAnisotropy = 1;
  s.sAddressMode = s.tAddressMode = s.rAddressMode = MTLSamplerAddressModeClampToEdge;
  _linear = [device newSamplerStateWithDescriptor:s];
  _blackLift = simd_make_float3(0.026f, 0.017f, 0.007f);    // a backlit panel, until told otherwise
  s.maxAnisotropy = 1;
  s.mipFilter = MTLSamplerMipFilterNotMipmapped;
  _clampLinear = [device newSamplerStateWithDescriptor:s];
  s.compareFunction = MTLCompareFunctionGreater;
  _shadowCompare = [device newSamplerStateWithDescriptor:s];

  MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kDepthFormat width:kShadowSize height:kShadowSize mipmapped:NO];
  td.storageMode = MTLStorageModePrivate;
  td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  _shadowMap = [device newTextureWithDescriptor:td];

  _particles = [[HSParticles alloc] initWithDevice:device library:library hdrFormat:kHdrFormat depthFormat:kDepthFormat cache:_cache];
  _text = [[HSTextCache alloc] initWithDevice:device];
  MTLRenderPipelineDescriptor *glyph = [MTLRenderPipelineDescriptor new];
  glyph.vertexFunction = [library newFunctionWithName:@"glyph_vertex"];
  glyph.fragmentFunction = [library newFunctionWithName:@"glyph_fragment"];
  glyph.colorAttachments[0].pixelFormat = kScreenFormat;
  glyph.colorAttachments[0].blendingEnabled = YES;
  glyph.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
  glyph.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  _glyph = [_cache render:glyph];
  if (!_particles || !_glyph) return nil;
  [_cache finish];

  id<MTLCounterSet> timestamps = nil;
  for (id<MTLCounterSet> set in device.counterSets) if ([set.name isEqualToString:MTLCommonCounterSetTimestamp]) timestamps = set;
  // Only for the tests that report them: the app itself has no use for them, and not every GPU counts at the same points.
  if (timestamps && getenv("HANDSONG_PASS_TIMES") && [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
    MTLCounterSampleBufferDescriptor *cd = [MTLCounterSampleBufferDescriptor new];
    cd.counterSet = timestamps;
    cd.storageMode = MTLStorageModeShared;
    cd.sampleCount = kPassCount * 4;
    _timers = [device newCounterSampleBufferWithDescriptor:cd error:NULL];
  }
  return self;
}

- (id<MTLTexture>)target:(MTLPixelFormat)format width:(NSUInteger)w height:(NSUInteger)h samples:(NSUInteger)samples
                 storage:(MTLStorageMode)storage usage:(MTLTextureUsage)usage {
  MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
  if (samples > 1) { d.textureType = MTLTextureType2DMultisample; d.sampleCount = samples; }
  d.storageMode = storage;
  d.usage = usage;
  return [_device newTextureWithDescriptor:d];
}

- (void)resizeTo:(NSUInteger)w :(NSUInteger)h {
  if (w == _width && h == _height) return;
  _width = w;
  _height = h;
  MTLTextureUsage target = MTLTextureUsageRenderTarget, readable = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  _msColor = [self target:kHdrFormat width:w height:h samples:4 storage:MTLStorageModeMemoryless usage:target];
  _msDepth = [self target:kDepthFormat width:w height:h samples:4 storage:MTLStorageModeMemoryless usage:target];
  _msColor2 = [self target:kHdrFormat width:w height:h samples:4 storage:MTLStorageModeMemoryless usage:target];
  _msDepth2 = [self target:kDepthFormat width:w height:h samples:4 storage:MTLStorageModeMemoryless usage:target];
  _opaque = [self target:kHdrFormat width:w height:h samples:1 storage:MTLStorageModePrivate usage:readable];
  _opaqueDepth = [self target:kDepthFormat width:w height:h samples:1 storage:MTLStorageModePrivate usage:readable];
  _hdr = [self target:kHdrFormat width:w height:h samples:1 storage:MTLStorageModePrivate usage:readable];
  // The glow must reach as far across the picture at any size, or a larger render is a different picture: it starts this many
  // levels further down, and the levels above it are only there on the way.
  _bloomFirst = 0;
  while ((h >> (_bloomFirst + 1)) >= 1664) _bloomFirst++;
  for (int i = 0; i < _bloomFirst + kBloomReach; i++) {
    NSUInteger bw = MAX(w >> (i + 1), 1u), bh = MAX(h >> (i + 1), 1u);
    MTLTextureUsage u = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    _down[i] = [self target:kHdrFormat width:bw height:bh samples:1 storage:MTLStorageModePrivate usage:u];
    _up[i] = [self target:kHdrFormat width:bw height:bh samples:1 storage:MTLStorageModePrivate usage:u];
  }
}

- (void)saveNextFrameTo:(NSString *)path { _shotPath = path; }

- (void)warmUpAt:(CGSize)size {
  static const simd_float4 pen[2] = {{0, 0.06f, 0.03f, 0}, {0.01f, 0.06f, 0.03f, 1}};
  hs_scene scene = hs_scene_default();
  scene.sand_amount = 1;
  scene.chladni_m = 2;
  scene.chladni_n = 3;
  scene.spawn_count = 2;
  scene.spawns[0] = (hs_particle_spawn){simd_make_float3(0, 0, 0), 0.001f, simd_make_float3(0, 0.01f, 0), 0.001f, simd_make_float3(0, 0, 0), 0.001f, 0.1f, HS_KIND_STEAM, 8, 0};
  scene.spawns[1] = (hs_particle_spawn){simd_make_float3(0, 0, 0), 0.001f, simd_make_float3(0, 0.01f, 0), 0.001f, simd_make_float3(0, 0, 0), 0.001f, 0.1f, HS_KIND_EMBER, 8, 0};
  scene.pen_points = pen;
  scene.pen_count = 2;
  scene.pen_progress = 1;
  scene.pen_brightness = 0;
  [self draw:&scene into:nil size:size];
  [self waitUntilIdle];
}

// How long this GPU takes over the heaviest picture in the story, at full clock, at this size: everything in view, close up, with the sand.
- (double)measureHeaviestAt:(CGSize)size {
  hs_scene scene = hs_scene_default();
  scene.sand_amount = 1;
  scene.chladni_m = 2;
  scene.chladni_n = 3;
  scene.eye = simd_make_float3(0, 0.104f, 0.0495f);
  scene.target = simd_make_float3(0, -0.006f, -0.010f);
  scene.fov_degrees = 28;
  double times[5];
  for (int i = 0; i < 16; i++) {
    [self draw:&scene into:nil size:size];
    [self waitUntilIdle];
    if (i >= 11) {
      int place = i - 11;
      double value = self.lastGpuSeconds * 1000;
      while (place > 0 && times[place - 1] > value) { times[place] = times[place - 1]; place--; }
      times[place] = value;
    }
  }
  return times[2];
}
- (void)prepareText:(NSString *)line key:(int)key { [_text prepare:line key:key]; }
- (void)waitForText { [_text waitUntilReady]; }
- (double)lastGpuSeconds { return atomic_load(&_lastGpu); }

- (void)resetPassReport {
  os_unfair_lock_lock(&_passLock);
  memset(_passSum, 0, sizeof _passSum);
  _passFrames = 0;
  os_unfair_lock_unlock(&_passLock);
}

- (NSString *)passReport {
  os_unfair_lock_lock(&_passLock);
  long n = MAX(_passFrames, 1);
  NSString *report = [NSString stringWithFormat:@"fragment ms: shadow %.2f opaque %.2f clear %.2f finish %.2f; bloom %.2f; vertex ms: shadow %.2f opaque %.2f clear %.2f",
          _passSum[0] / n, _passSum[1] / n, _passSum[2] / n, _passSum[3] / n, _passSum[4] / n, _passSum[5] / n, _passSum[6] / n, _passSum[7] / n];
  os_unfair_lock_unlock(&_passLock);
  return report;
}

- (void)waitUntilIdle {
  for (int i = 0; i < kInFlight; i++) dispatch_semaphore_wait(_inFlight, DISPATCH_TIME_FOREVER);
  for (int i = 0; i < kInFlight; i++) dispatch_semaphore_signal(_inFlight);
}

static void attach_timers(MTLRenderPassDescriptor *pass, id<MTLCounterSampleBuffer> timers, int index) {
  if (!timers) return;
  pass.sampleBufferAttachments[0].sampleBuffer = timers;
  pass.sampleBufferAttachments[0].startOfVertexSampleIndex = (NSUInteger)index * 4;
  pass.sampleBufferAttachments[0].endOfVertexSampleIndex = (NSUInteger)index * 4 + 1;
  pass.sampleBufferAttachments[0].startOfFragmentSampleIndex = (NSUInteger)index * 4 + 2;
  pass.sampleBufferAttachments[0].endOfFragmentSampleIndex = (NSUInteger)index * 4 + 3;
}

// What cannot reach the picture is not drawn at all. In a close shot that is most of the vertices, just when the pixels cost the most.
static bool part_in_view(const hs_part *part, const hs_draw *draw, const simd_float4 planes[5], float margin) {
  simd_float3 lo = simd_make_float3(part->bounds_min[0], part->bounds_min[1], part->bounds_min[2]), hi = simd_make_float3(part->bounds_max[0], part->bounds_max[1], part->bounds_max[2]);
  // A moving part's own travel is small against the margin added here, and a crew member's bone moves its whole box with it.
  simd_float3 center = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
  simd_float4 world = simd_mul(draw->model, simd_make_float4(center, 1));
  float radius = simd_length(half) + 0.004f + margin;
  for (int i = 0; i < 5; i++) if (simd_dot(planes[i], simd_make_float4(world.xyz, 1)) < -radius) return false;
  return true;
}

// Only the controls stand outside the clear body: everything else lies inside it, behind its near wall from any side, and so cannot hide it.
static bool stands_outside(const char *name) {
  static const char *const outside[] = {"pack_", "player_light", "mic_lamp", "stick_", "key_", "cross", "trigger_", "shoulder_", "pill_", "home", "mute", "bezel", "filament", "gauge", "needle"};
  for (unsigned i = 0; i < sizeof outside / sizeof outside[0]; i++) if (!strncmp(name, outside[i], strlen(outside[i]))) return true;
  return false;
}

- (void)drawParts:(id<MTLRenderCommandEncoder>)encoder pass:(hs_part_pass)pass drawOffset:(NSUInteger)base buffer:(id<MTLBuffer>)buffer {
  const hs_draw *draws = (const hs_draw *)((const char *)buffer.contents + base);
  for (uint32_t i = 0; i < _pack->part_count; i++) {
    const hs_part *part = &_pack->parts[i];
    if (part->pass != (int32_t)pass || (_hidden && _hidden[i]) || part->index_count <= 3) continue;
    if (!part_in_view(part, &draws[i], _planes, _cullMargin)) continue;
    if (_onlyOutside && !_standsOutside[i]) continue;
    [encoder setVertexBufferOffset:part->vertex_offset * sizeof(hs_vertex) atIndex:0];
    [encoder setVertexBuffer:buffer offset:base + i * sizeof(hs_draw) atIndex:1];
    [encoder setFragmentBuffer:buffer offset:base + i * sizeof(hs_draw) atIndex:1];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:part->index_count indexType:MTLIndexTypeUInt32 indexBuffer:_indices
                 indexBufferOffset:part->index_offset * sizeof(uint32_t)];
  }
}

- (void)draw:(const hs_scene *)scene into:(id<CAMetalDrawable>)drawable size:(CGSize)size {
  id<MTLTexture> screen = drawable.texture;
  if (!screen) {
    if (!_offscreen || _offscreen.width != (NSUInteger)size.width || _offscreen.height != (NSUInteger)size.height)
      _offscreen = [self target:kScreenFormat width:(NSUInteger)size.width height:(NSUInteger)size.height samples:1 storage:MTLStorageModePrivate usage:MTLTextureUsageRenderTarget];
    screen = _offscreen;
  }
  [self resizeTo:screen.width :screen.height];
  dispatch_semaphore_wait(_inFlight, DISPATCH_TIME_FOREVER);
  unsigned index = _frameIndex++ % kInFlight;

  hs_frame frame;
  memset(&frame, 0, sizeof frame);
  simd_float4x4 view = look_at(scene->eye, scene->target, simd_make_float3(0, 1, 0));
  float aspect = (float)screen.width / (float)screen.height;
  frame.view = view;
  frame.projection = perspective(scene->fov_degrees * (float)M_PI / 180, aspect, kNearPlane);
  frame.near_plane = kNearPlane;
  frame.view_projection = simd_mul(frame.projection, view);
  frame.inverse_view_projection = simd_inverse(frame.view_projection);
  simd_float3 key = scene->light_count > 0 ? simd_normalize(scene->lights[0].position) : simd_make_float3(0.3f, 0.9f, 0.3f);
  simd_float3 focus = simd_make_float3(scene->object.columns[3].x, scene->object.columns[3].y, scene->object.columns[3].z);
  simd_float3 light_up = fabsf(key.y) > 0.9f ? simd_make_float3(0, 0, 1) : simd_make_float3(0, 1, 0);
  frame.light_view_projection = simd_mul(orthographic(0.14f, 0.02f, 0.70f), look_at(focus + key * 0.35f, focus, light_up));
  frame.camera = scene->eye;
  frame.time = (float)fmod(scene->seconds, 14400);
  frame.size = simd_make_float2((float)screen.width, (float)screen.height);
  frame.one_over_size = simd_make_float2(1.0f / (float)screen.width, 1.0f / (float)screen.height);
  frame.env_yaw = scene->env_yaw;
  frame.exposure = scene->exposure;
  frame.env_strength = scene->env_strength;
  frame.dim = scene->dim;
  frame.light_count = scene->light_count;
  memcpy(frame.lights, scene->lights, sizeof frame.lights);
  frame.capsule_count = scene->capsule_count;
  memcpy(frame.capsules, scene->capsules, sizeof frame.capsules);
  frame.backdrop_glow = scene->backdrop_glow;
  frame.shadow_strength = scene->shadow_strength;
  frame.horizon_at = 0.56f;
  frame.horizon_low = simd_make_float3(0.052f, 0.022f, 0.016f);
  frame.horizon_high = simd_make_float3(0.018f, 0.009f, 0.008f);
  frame.filament = scene->filament;
  frame.filament_color = scene->filament_tint.x + scene->filament_tint.y + scene->filament_tint.z > 0 ? scene->filament_tint : simd_make_float3(1.0f, 0.46f, 0.18f);
  frame.touch_color = scene->touch_color;
  frame.black_lift = _blackLift;
  frame.caption_shade = scene->caption_shade;
  frame.touch[0] = scene->touch[0];
  frame.touch[1] = scene->touch[1];
  memcpy(frame.ripple, scene->ripple, sizeof frame.ripple);
  frame.lattice_wake = scene->lattice_wake;
  memcpy(frame.sh, _resources.irradiance, sizeof frame.sh);

  hs_draw *draws = _drawBuffer[index].contents;
  hs_rig_compute(_pack, scene, draws);
  _hidden = scene->part_hidden;

  id<MTLCommandBuffer> commands = [_queue commandBuffer];
  [_particles stepInto:commands scene:scene];
  {
    // The frustum's planes in the world, from the view-projection's rows; reversed depth to infinity needs no far plane.
    simd_float4x4 m = simd_transpose(frame.view_projection);
    _planes[0] = m.columns[3] + m.columns[0];
    _planes[1] = m.columns[3] - m.columns[0];
    _planes[2] = m.columns[3] + m.columns[1];
    _planes[3] = m.columns[3] - m.columns[1];
    _planes[4] = m.columns[3] - m.columns[2];
    for (int i = 0; i < 5; i++) _planes[i] /= simd_length(_planes[i].xyz);
  }
  // ---------------------------------------------------------------- Shadow
  MTLRenderPassDescriptor *shadow = [MTLRenderPassDescriptor renderPassDescriptor];
  shadow.depthAttachment.texture = _shadowMap;
  shadow.depthAttachment.loadAction = MTLLoadActionClear;
  shadow.depthAttachment.clearDepth = 0;
  shadow.depthAttachment.storeAction = MTLStoreActionStore;
  attach_timers(shadow, _timers, 0);
  id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:shadow];
  _cullMargin = 0.07f;       // what is just out of view can still cast a shadow into it; further than the object is deep, it cannot
  [e setRenderPipelineState:_shadow];
  [e setDepthStencilState:_depthWrite];
  [e setFrontFacingWinding:MTLWindingCounterClockwise];
  [e setCullMode:MTLCullModeBack];
  [e setVertexBuffer:_vertices offset:0 atIndex:0];
  [e setVertexBytes:&frame length:sizeof frame atIndex:2];
  [self drawParts:e pass:HS_PASS_SOLID drawOffset:0 buffer:_drawBuffer[index]];
  [e endEncoding];

  // ---------------------------------------------------------------- The interior and every solid part
  MTLRenderPassDescriptor *opaque = [MTLRenderPassDescriptor renderPassDescriptor];
  opaque.colorAttachments[0].texture = _msColor;
  opaque.colorAttachments[0].resolveTexture = _opaque;
  opaque.colorAttachments[0].loadAction = MTLLoadActionClear;
  opaque.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
  opaque.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
  opaque.depthAttachment.texture = _msDepth;
  opaque.depthAttachment.loadAction = MTLLoadActionClear;
  opaque.depthAttachment.clearDepth = 0;
  opaque.depthAttachment.storeAction = MTLStoreActionDontCare;
  if (scene->spawn_count > 0 || scene->pen_count > 0 || scene->seconds < _particlesUntil) {
    // Only what moves in the air needs the depth afterwards, and resolving it is the costliest thing in the frame.
    if (scene->spawn_count > 0) _particlesUntil = scene->seconds + 6;
    opaque.depthAttachment.resolveTexture = _opaqueDepth;
    opaque.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
    opaque.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterMax;
  }
  attach_timers(opaque, _timers, 1);
  _cullMargin = 0;
  e = [commands renderCommandEncoderWithDescriptor:opaque];
  [e setVertexBuffer:_vertices offset:0 atIndex:0];
  [e setVertexBytes:&frame length:sizeof frame atIndex:2];
  [e setFragmentBytes:&frame length:sizeof frame atIndex:2];
  [e setFragmentBuffer:_materials offset:0 atIndex:3];
  [e setFragmentTexture:_resources.environment atIndex:0];
  [e setFragmentTexture:_shadowMap atIndex:1];
  [e setFragmentTexture:_resources.blueNoise atIndex:2];
  [e setFragmentSamplerState:_linear atIndex:0];
  [e setFragmentSamplerState:_shadowCompare atIndex:1];
  [e setRenderPipelineState:_surface];
  [e setDepthStencilState:_depthWrite];
  [e setFrontFacingWinding:MTLWindingCounterClockwise];
  [e setCullMode:MTLCullModeBack];
  [self drawParts:e pass:HS_PASS_SOLID drawOffset:0 buffer:_drawBuffer[index]];
  [e setDepthStencilState:_depthRead];
  [e setCullMode:MTLCullModeNone];
  [_particles drawSandInto:e scene:scene frame:&frame];
  [e setRenderPipelineState:_backdrop];
  [e setDepthStencilState:_depthNone];
  [e setCullMode:MTLCullModeNone];
  [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  // The far walls of the clear parts, over everything behind them: only what they reflect.
  [e setRenderPipelineState:_clearBack];
  [e setDepthStencilState:_depthRead];
  [e setCullMode:MTLCullModeNone];
  [e setVertexBuffer:_vertices offset:0 atIndex:0];
  [self drawParts:e pass:HS_PASS_CLEAR_BACK drawOffset:0 buffer:_drawBuffer[index]];
  [e endEncoding];

  // ---------------------------------------------------------------- What moves in the air, seen through the clear body as well
  [_particles encodeInto:commands scene:scene frame:&frame opaque:_opaque depth:_opaqueDepth linear:_clampLinear];

  // ---------------------------------------------------------------- The clear body, bending all of that
  MTLRenderPassDescriptor *hero = [MTLRenderPassDescriptor renderPassDescriptor];
  hero.colorAttachments[0].texture = _msColor2;
  hero.colorAttachments[0].resolveTexture = _hdr;
  hero.colorAttachments[0].loadAction = MTLLoadActionDontCare;
  hero.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
  hero.depthAttachment.texture = _msDepth2;
  hero.depthAttachment.loadAction = MTLLoadActionClear;
  hero.depthAttachment.clearDepth = 0;
  hero.depthAttachment.storeAction = MTLStoreActionDontCare;
  attach_timers(hero, _timers, 2);
  e = [commands renderCommandEncoderWithDescriptor:hero];
  [e setRenderPipelineState:_copy];
  [e setDepthStencilState:_depthNone];
  [e setFragmentTexture:_opaque atIndex:0];
  [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  [e setVertexBuffer:_vertices offset:0 atIndex:0];
  [e setVertexBytes:&frame length:sizeof frame atIndex:2];
  [e setFrontFacingWinding:MTLWindingCounterClockwise];
  [e setCullMode:MTLCullModeBack];
  [e setDepthStencilState:_depthWrite];
  [e setRenderPipelineState:_depthOnly];
  _onlyOutside = true;
  [self drawParts:e pass:HS_PASS_SOLID drawOffset:0 buffer:_drawBuffer[index]];
  _onlyOutside = false;
  [e setRenderPipelineState:_clear];
  [e setFragmentBytes:&frame length:sizeof frame atIndex:2];
  [e setFragmentTexture:_opaque atIndex:0];
  [e setFragmentTexture:_resources.environment atIndex:1];
  [e setFragmentSamplerState:_linear atIndex:0];
  [self drawParts:e pass:HS_PASS_CLEAR drawOffset:0 buffer:_drawBuffer[index]];
  [e endEncoding];

  // ---------------------------------------------------------------- Bloom
  MTLComputePassDescriptor *bloom_pass = [MTLComputePassDescriptor computePassDescriptor];
  if (_timers) {
    bloom_pass.sampleBufferAttachments[0].sampleBuffer = _timers;
    bloom_pass.sampleBufferAttachments[0].startOfEncoderSampleIndex = 4 * 4;
    bloom_pass.sampleBufferAttachments[0].endOfEncoderSampleIndex = 4 * 4 + 1;
  }
  id<MTLComputeCommandEncoder> c = [commands computeCommandEncoderWithDescriptor:bloom_pass];
  [c setComputePipelineState:_bloomDown];
  [c setSamplerState:_clampLinear atIndex:0];
  int bloom_levels = _bloomFirst + kBloomReach;
  for (int i = 0; i < bloom_levels; i++) {
    int from_screen = i == 0;
    [c setBytes:&from_screen length:sizeof from_screen atIndex:0];
    [c setTexture:i ? _down[i - 1] : _hdr atIndex:0];
    [c setTexture:_down[i] atIndex:1];
    [c dispatchThreads:MTLSizeMake(_down[i].width, _down[i].height, 1) threadsPerThreadgroup:MTLSizeMake(32, 32, 1)];
  }
  [c setComputePipelineState:_bloomUp];
  for (int i = bloom_levels - 2; i >= _bloomFirst; i--) {
    [c setTexture:i == bloom_levels - 2 ? _down[bloom_levels - 1] : _up[i + 1] atIndex:0];
    [c setTexture:_down[i] atIndex:1];
    [c setTexture:_up[i] atIndex:2];
    [c dispatchThreads:MTLSizeMake(_up[i].width, _up[i].height, 1) threadsPerThreadgroup:MTLSizeMake(32, 32, 1)];
  }
  [c endEncoding];

  // ---------------------------------------------------------------- To the panel
  void (^finish)(id<MTLTexture>, id<MTLRenderPipelineState>) = ^(id<MTLTexture> into, id<MTLRenderPipelineState> state) {
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = into;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (into == screen) attach_timers(pass, self->_timers, 3);
    id<MTLRenderCommandEncoder> f = [commands renderCommandEncoderWithDescriptor:pass];
    [f setRenderPipelineState:state];
    [f setFragmentBytes:&frame length:sizeof frame atIndex:2];
    [f setFragmentTexture:self->_hdr atIndex:0];
    [f setFragmentTexture:self->_up[self->_bloomFirst] atIndex:1];
    [f setFragmentTexture:self->_resources.grade atIndex:2];
    [f setFragmentTexture:self->_resources.blueNoise atIndex:3];
    [f setFragmentSamplerState:self->_clampLinear atIndex:0];
    [f drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    // Words are drawn last, over the finished picture, from a distance field: crisp at the panel's own pixels.
    void (^say)(int, float, float, float, float) = ^(int key, float alpha, float x, float y, float size) {
      id<MTLTexture> line = key >= 0 && alpha > 0 ? [self->_text textureForKey:key] : nil;
      if (!line) return;
      float height = size * (float)into.height / 1664.0f * 2.2f;
      float width = height * (float)line.width / (float)line.height;
      hs_glyph glyph = {simd_make_float4(x * (float)into.width, y * (float)into.height - height * 0.7f, width, height), simd_make_float4(0, 0, 1, 1),
                        simd_make_float4(0.92f, 0.87f, 0.80f, alpha)};
      [f setRenderPipelineState:self->_glyph];
      [f setVertexBytes:&glyph length:sizeof glyph atIndex:0];
      [f setVertexBytes:&frame length:sizeof frame atIndex:2];
      [f setFragmentTexture:line atIndex:0];
      [f setFragmentSamplerState:self->_clampLinear atIndex:0];
      [f drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    };
    say(scene->text_key, scene->text_alpha, scene->text_x, scene->text_y, scene->text_size);
    say(scene->nav_key, scene->nav_alpha, 0.07f, 0.945f, 28);
    [f endEncoding];
  };
  finish(screen, _finish);

  id<MTLTexture> shot = nil;
  NSString *shotPath = _shotPath;
  void (^sink)(id<MTLTexture>) = _frameSink;
  if (shotPath || sink) {
    _shotPath = nil;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kScreenFormat width:screen.width height:screen.height mipmapped:NO];
    d.storageMode = MTLStorageModeShared;
    d.usage = MTLTextureUsageRenderTarget;
    shot = [_device newTextureWithDescriptor:d];
    finish(shot, _finish);
  }
  if (drawable) [commands presentDrawable:drawable];
  dispatch_semaphore_t inFlight = _inFlight;
  [commands addCompletedHandler:^(id<MTLCommandBuffer> finished) {
    atomic_store(&self->_lastGpu, finished.GPUEndTime - finished.GPUStartTime);
    if (self->_timers) {
      NSData *times = [self->_timers resolveCounterRange:NSMakeRange(0, kPassCount * 4)];
      if (times.length >= kPassCount * 32) {
        const uint64_t *t = times.bytes;
        os_unfair_lock_lock(&self->_passLock);      // handlers of different frames may finish on different threads
        for (int i = 0; i < 4; i++) {
          uint64_t vs = t[i * 4], ve = t[i * 4 + 1], fs = t[i * 4 + 2], fe = t[i * 4 + 3];
          if (fe > fs && fe != ~0ull) self->_passSum[i] += (double)(fe - fs) / 1e6;
          if (ve > vs && ve != ~0ull) self->_passSum[i + kPassCount] += (double)(ve - vs) / 1e6;
        }
        if (t[17] > t[16] && t[17] != ~0ull) self->_passSum[4] += (double)(t[17] - t[16]) / 1e6;
        self->_passFrames++;
        os_unfair_lock_unlock(&self->_passLock);
      }
    }
    if (shot && shotPath) hs_save_png(shot, shotPath);
    if (shot && sink) sink(shot);
    dispatch_semaphore_signal(inFlight);
  }];
  [commands commit];
}
@end
