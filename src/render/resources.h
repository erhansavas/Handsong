// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What the renderer reads from disk: the stage's light, the colour table, the noise.
#pragma once
#import <Metal/Metal.h>
#include "../../shaders/shared.h"

@interface HSResources : NSObject
- (nullable instancetype)initWithDevice:(nonnull id<MTLDevice>)device queue:(nonnull id<MTLCommandQueue>)queue folder:(nonnull NSString *)folder;
@property(nonatomic, readonly, nonnull) id<MTLTexture> environment, grade, blueNoise;
@property(nonatomic, readonly, nonnull) const simd_float4 *irradiance;
@end

const hs_material *_Nonnull hs_material_table(void);
void hs_save_png(id<MTLTexture> _Nonnull texture, NSString *_Nonnull path);
