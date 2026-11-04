// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Every pipeline built once, kept in a binary archive tied to this machine and this system, so that nothing is ever compiled in front
// of the player. When the system changes the archive is simply built again, on the next launch's loading screen of black.
#pragma once
#import <Metal/Metal.h>

@interface HSPipelineCache : NSObject
- (nonnull instancetype)initWithDevice:(nonnull id<MTLDevice>)device library:(nonnull id<MTLLibrary>)library;
- (nullable id<MTLRenderPipelineState>)render:(nonnull MTLRenderPipelineDescriptor *)descriptor;
- (nullable id<MTLComputePipelineState>)compute:(nonnull NSString *)function;
- (void)finish;          // keeps what was new
@property(nonatomic, readonly) long misses;
@end
