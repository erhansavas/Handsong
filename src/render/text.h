// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// A few lines of words, each kept as a distance field. The face is the system's own serif, drawn at run time; no font ships here.
#pragma once
#import <Metal/Metal.h>

@interface HSTextCache : NSObject
- (nonnull instancetype)initWithDevice:(nonnull id<MTLDevice>)device;
- (void)prepare:(nonnull NSString *)line key:(int)key;
- (nullable id<MTLTexture>)textureForKey:(int)key;
- (void)waitUntilReady;
@end
