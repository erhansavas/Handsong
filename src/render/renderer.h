// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The picture, for one chip. Forward on tile memory at 4x, the interior resolved first and the clear body drawn over it.
#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include "render/mesh_pack.h"
#include "render/scene.h"

@interface HSRenderer : NSObject
- (nullable instancetype)initWithDevice:(nonnull id<MTLDevice>)device
                              resources:(nonnull NSString *)resources
                               meshPack:(nonnull const hs_mesh_pack *)pack;
// With no drawable it draws to its own target of the given size, as tests do.
- (void)draw:(nonnull const hs_scene *)scene into:(nullable id<CAMetalDrawable>)drawable size:(CGSize)size;
- (void)saveNextFrameTo:(nonnull NSString *)path;
@property(nonatomic, copy, nullable) void (^frameSink)(id<MTLTexture> _Nonnull);   // given every finished frame, in tests that make a movie
- (void)prepareText:(nonnull NSString *)line key:(int)key;   // rasterised off the main thread, ready before it is needed
- (void)waitUntilIdle;
- (void)waitForText;
- (void)warmUpAt:(CGSize)size;      // one hidden frame with every kind of work in it, so that nothing is touched for the first time on screen
- (double)measureHeaviestAt:(CGSize)size;
@property(nonatomic) simd_float3 blackLift;   // added to the darkest values at the very end, in the panel's own encoded terms
@property(nonatomic, readonly) double lastGpuSeconds;
@property(nonatomic, readonly, nonnull) NSString *passReport;
- (void)resetPassReport;
@end
