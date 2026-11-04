// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "render/particles.h"

enum { kSandCount = 30000, kSparkFirst = kSandCount, kSparkCount = HS_PARTICLE_FINE - kSandCount, kSoftFirst = HS_PARTICLE_FINE, kPenCapacity = 1024, kInFlightPen = 3 };

@implementation HSParticles {
  id<MTLDevice> _device;
  id<MTLComputePipelineState> _spawn, _step;
  id<MTLRenderPipelineState> _fine, _soft, _pen, _composite, _sand;
  bool _sandTurn;
  id<MTLBuffer> _particles, _penPoints[kInFlightPen];
  id<MTLTexture> _half;
  unsigned _sparkHead, _softHead, _tick, _penIndex;
  unsigned _sparkUsed, _softUsed;      // how far into each pool anything has been spawned since it last went quiet: all that needs work
  bool _wasBusy;
  double _busyUntil, _time;
  MTLPixelFormat _format;
  __weak HSPipelineCache *_cacheInUse;
}

- (id<MTLRenderPipelineState>)additive:(id<MTLLibrary>)library vertex:(NSString *)vertex fragment:(NSString *)fragment {
  MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
  d.vertexFunction = [library newFunctionWithName:vertex];
  d.fragmentFunction = [library newFunctionWithName:fragment];
  d.colorAttachments[0].pixelFormat = _format;
  d.colorAttachments[0].blendingEnabled = YES;
  d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
  d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
  return [_cacheInUse render:d];
}

- (instancetype)initWithDevice:(id<MTLDevice>)device library:(id<MTLLibrary>)library hdrFormat:(MTLPixelFormat)format depthFormat:(MTLPixelFormat)depth_format cache:(HSPipelineCache *)cache {
  if (!(self = [super init])) return nil;
  _device = device;
  _format = format;
  _cacheInUse = cache;
  _spawn = [cache compute:@"particle_spawn"];
  _step = [cache compute:@"particle_step"];
  _fine = [self additive:library vertex:@"particle_vertex" fragment:@"particle_fragment"];
  _soft = _fine;
  _pen = [self additive:library vertex:@"pen_vertex" fragment:@"pen_fragment"];
  _composite = [self additive:library vertex:@"screen_vertex" fragment:@"soft_composite"];
  MTLRenderPipelineDescriptor *sand = [MTLRenderPipelineDescriptor new];
  sand.vertexFunction = [library newFunctionWithName:@"particle_vertex"];
  sand.fragmentFunction = [library newFunctionWithName:@"sand_fragment"];
  sand.rasterSampleCount = 4;
  sand.depthAttachmentPixelFormat = depth_format;
  sand.colorAttachments[0].pixelFormat = _format;
  sand.colorAttachments[0].blendingEnabled = YES;
  sand.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
  sand.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
  _sand = [cache render:sand];
  if (!_spawn || !_step || !_fine || !_pen || !_composite || !_sand) return nil;
  _particles = [device newBufferWithLength:sizeof(hs_particle) * HS_PARTICLES options:MTLResourceStorageModeShared];
  hs_particle *p = _particles.contents;
  uint32_t random = 271828;
  for (int i = 0; i < kSandCount; i++) {
    // Evenly over the disc of the plate. Their order in the buffer is the order they appear in as sand is poured on, so that the work
    // rises with the sand and the GPU's clock can follow it.
    random = random * 1664525u + 1013904223u;
    float a = (float)(random >> 8) / 16777216.0f * 6.2831853f;
    random = random * 1664525u + 1013904223u;
    float r = sqrtf((float)(random >> 8) / 16777216.0f) * 0.97f;
    float seed = ((float)i + 0.5f) / (float)kSandCount;
    random = random * 1664525u + 1013904223u;
    float shade = (float)(random >> 8) / 16777216.0f;
    p[i] = (hs_particle){.position = simd_make_float3(cosf(a) * r * 0.0112f, -0.0052f, -0.010f + sinf(a) * r * 0.0112f), .life = 0, .age = 0, .color = simd_make_float3(0.85f, 0.70f, 0.48f) * (0.08f + shade * 0.18f), .size = 0.00009f, .kind = HS_KIND_SAND, .seed = seed};
  }
  for (int i = 0; i < kInFlightPen; i++) _penPoints[i] = [device newBufferWithLength:sizeof(simd_float4) * kPenCapacity options:MTLResourceStorageModeShared];
  _busyUntil = -1;
  return self;
}

static hs_particle_step step_for(const hs_scene *scene, float dt, double now) {
  hs_particle_step step = {};
  step.object = scene->object;
  // Gravity as this object feels it: its own idea of down.
  simd_float4x4 inverse = simd_inverse(scene->object);
  simd_float4 down = simd_mul(inverse, simd_make_float4(0, -9.81f, 0, 0));
  step.gravity = down.xyz;
  step.dt = dt;
  step.time = (float)fmod(now, 1000);
  step.chladni_m = scene->chladni_m;
  step.chladni_n = scene->chladni_n;
  step.chladni_shake = scene->chladni_shake;
  step.plate_center = simd_make_float3(0, -0.0052f, -0.010f);
  step.plate_radius = 0.0112f;
  step.plate_up = simd_make_float3(0, 1, 0);
  step.sand_amount = scene->sand_amount;
  return step;
}

static NSUInteger sand_poured(const hs_scene *scene) { return MIN((NSUInteger)kSandCount, (NSUInteger)((double)kSandCount * (double)scene->sand_amount) + 1); }

- (void)stepInto:(id<MTLCommandBuffer>)commands scene:(const hs_scene *)scene {
  bool sand = scene->sand_amount > 0.001f;
  double now = scene->seconds;
  float dt = (float)fmin(fmax(now - _time, 0.0), 0.05);
  _time = now;
  if (scene->spawn_count > 0) _busyUntil = now + 6;
  bool sparks = now < _busyUntil;
  if (_wasBusy && !sparks) _sparkHead = _softHead = _sparkUsed = _softUsed = 0;    // everything has lived its life by now
  _wasBusy = sparks;
  if (!sand && !sparks) return;
  hs_particle_step step = step_for(scene, dt, now);
  id<MTLComputeCommandEncoder> c = [commands computeCommandEncoder];
  [c setBuffer:_particles offset:0 atIndex:0];
  if (scene->spawn_count > 0) {
    [c setComputePipelineState:_spawn];
    _tick++;
    for (int i = 0; i < scene->spawn_count; i++) {
      hs_particle_spawn s = scene->spawns[i];
      bool soft = s.kind == HS_KIND_STEAM;
      unsigned start = soft ? kSoftFirst : kSparkFirst, capacity = soft ? HS_PARTICLE_SOFT : kSparkCount;
      unsigned *head = soft ? &_softHead : &_sparkHead;
      unsigned first = *head;
      unsigned *used = soft ? &_softUsed : &_sparkUsed;
      *used = MIN(capacity, MAX(*used, first + (unsigned)s.count));
      *head = (*head + (unsigned)s.count) % capacity;
      [c setBytes:&s length:sizeof s atIndex:1];
      [c setBytes:&first length:sizeof first atIndex:2];
      [c setBytes:&start length:sizeof start atIndex:3];
      [c setBytes:&capacity length:sizeof capacity atIndex:4];
      unsigned tick = _tick + (unsigned)i * 131;
      [c setBytes:&tick length:sizeof tick atIndex:5];
      [c dispatchThreads:MTLSizeMake((NSUInteger)s.count, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    }
    [c memoryBarrierWithScope:MTLBarrierScopeBuffers];
  }
  [c setComputePipelineState:_step];
  [c setBytes:&step length:sizeof step atIndex:1];
  // Sand only while the plate has it, half of the poured grains on each frame: they wander slowly enough that thirty steps a second look the same.
  if (sand) {
    NSUInteger poured = sand_poured(scene), half = poured / 2 + 1;
    _sandTurn = !_sandTurn;
    NSUInteger from = _sandTurn ? 0 : half, count = _sandTurn ? half : poured - MIN(half, poured);
    hs_particle_step sand_step = step;
    sand_step.dt = step.dt * 2;
    if (count > 0) {
      [c setBytes:&sand_step length:sizeof sand_step atIndex:1];
      [c setBuffer:_particles offset:from * sizeof(hs_particle) atIndex:0];
      [c dispatchThreads:MTLSizeMake(count, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
      [c setBytes:&step length:sizeof step atIndex:1];
    }
  }
  // The rest only while something is alive, and only as far as anything was spawned.
  if (sparks && _sparkUsed > 0) {
    [c setBuffer:_particles offset:kSparkFirst * sizeof(hs_particle) atIndex:0];
    [c dispatchThreads:MTLSizeMake(_sparkUsed, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
  }
  if (sparks && _softUsed > 0) {
    [c setBuffer:_particles offset:kSoftFirst * sizeof(hs_particle) atIndex:0];
    [c dispatchThreads:MTLSizeMake(_softUsed, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
  }
  [c endEncoding];
}

- (void)drawSandInto:(id<MTLRenderCommandEncoder>)e scene:(const hs_scene *)scene frame:(const hs_frame *)frame {
  if (scene->sand_amount <= 0.001f) return;
  hs_particle_step step = step_for(scene, 0, scene->seconds);
  [e setRenderPipelineState:_sand];
  [e setVertexBuffer:_particles offset:0 atIndex:0];
  [e setVertexBytes:frame length:sizeof *frame atIndex:2];
  [e setVertexBytes:&step length:sizeof step atIndex:3];
  [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:sand_poured(scene)];
}

- (void)encodeInto:(id<MTLCommandBuffer>)commands scene:(const hs_scene *)scene frame:(const hs_frame *)frame opaque:(id<MTLTexture>)opaque depth:(id<MTLTexture>)depth linear:(id<MTLSamplerState>)linear {
  double now = scene->seconds;
  bool sparks = now < _busyUntil;
  if (!sparks && scene->pen_count == 0) return;
  hs_particle_step step = step_for(scene, 0, now);

  if (!_half || _half.width != opaque.width / 2 || _half.height != opaque.height / 2) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:_format width:opaque.width / 2 height:opaque.height / 2 mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    _half = [_device newTextureWithDescriptor:d];
  }
  bool plumes = sparks && _softUsed > 0;
  if (plumes) {
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = _half;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:pass];
    [e setRenderPipelineState:_soft];
    [e setVertexBuffer:_particles offset:kSoftFirst * sizeof(hs_particle) atIndex:0];
    [e setVertexBytes:frame length:sizeof *frame atIndex:2];
    [e setVertexBytes:&step length:sizeof step atIndex:3];
    [e setFragmentBytes:frame length:sizeof *frame atIndex:2];
    float depth_scale = 2;
    [e setFragmentBytes:&depth_scale length:sizeof depth_scale atIndex:1];
    [e setFragmentTexture:depth atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:_softUsed];
    [e endEncoding];
  }
  MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = opaque;
  pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:pass];
  [e setVertexBytes:frame length:sizeof *frame atIndex:2];
  [e setVertexBytes:&step length:sizeof step atIndex:3];
  [e setFragmentBytes:frame length:sizeof *frame atIndex:2];
  [e setFragmentTexture:depth atIndex:0];
  float depth_scale = 1;
  [e setFragmentBytes:&depth_scale length:sizeof depth_scale atIndex:1];
  if (sparks && _sparkUsed > 0) {
    [e setRenderPipelineState:_fine];
    [e setVertexBuffer:_particles offset:kSparkFirst * sizeof(hs_particle) atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:_sparkUsed];
  }
  if (plumes) {
    [e setRenderPipelineState:_composite];
    [e setFragmentTexture:_half atIndex:0];
    [e setFragmentSamplerState:linear atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e setFragmentTexture:depth atIndex:0];
  }
  if (scene->pen_count > 1 && scene->pen_brightness > 0) {
    id<MTLBuffer> points = _penPoints[_penIndex++ % kInFlightPen];
    int count = MIN(scene->pen_count, kPenCapacity);
    memcpy(points.contents, scene->pen_points, sizeof(simd_float4) * (size_t)count);
    simd_float2 pen = simd_make_float2(scene->pen_progress, scene->pen_brightness);
    [e setRenderPipelineState:_pen];
    [e setVertexBuffer:points offset:0 atIndex:0];
    [e setVertexBytes:&pen length:sizeof pen atIndex:1];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:(NSUInteger)count - 1];
  }
  [e endEncoding];
}
@end
