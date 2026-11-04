// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Particles on the GPU, and the pen. Fine light goes straight onto the resolved interior; steam goes through half resolution.
#pragma once
#import <Metal/Metal.h>
#import "render/pipeline_cache.h"
#include "render/scene.h"

@interface HSParticles : NSObject
- (nullable instancetype)initWithDevice:(nonnull id<MTLDevice>)device library:(nonnull id<MTLLibrary>)library hdrFormat:(MTLPixelFormat)format depthFormat:(MTLPixelFormat)depth cache:(nonnull HSPipelineCache *)cache;
// Sand lies on the plate: it is drawn with the parts, at four samples, behind whatever stands in front of it.
- (void)drawSandInto:(nonnull id<MTLRenderCommandEncoder>)encoder scene:(nonnull const hs_scene *)scene frame:(nonnull const hs_frame *)frame;
- (void)stepInto:(nonnull id<MTLCommandBuffer>)commands scene:(nonnull const hs_scene *)scene;
- (void)encodeInto:(nonnull id<MTLCommandBuffer>)commands
             scene:(nonnull const hs_scene *)scene
             frame:(nonnull const hs_frame *)frame
            opaque:(nonnull id<MTLTexture>)opaque
             depth:(nonnull id<MTLTexture>)depth
            linear:(nonnull id<MTLSamplerState>)linear;
@end
