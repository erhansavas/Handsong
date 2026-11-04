// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "render/pipeline_cache.h"

@implementation HSPipelineCache {
  id<MTLDevice> _device;
  id<MTLLibrary> _library;
  id<MTLBinaryArchive> _archive;
  NSURL *_url;
  bool _existing;
  bool _changed;
  long _misses;
}

- (long)misses { return _misses; }

- (instancetype)initWithDevice:(id<MTLDevice>)device library:(id<MTLLibrary>)library {
  if (!(self = [super init])) return self;
  _device = device;
  _library = library;
  NSString *caches = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"io.github.erhansavas.handsong"];
  [NSFileManager.defaultManager createDirectoryAtPath:caches withIntermediateDirectories:YES attributes:nil error:NULL];
  NSString *resources = NSBundle.mainBundle.resourcePath;
  NSDictionary *shaders = [NSFileManager.defaultManager attributesOfItemAtPath:[resources stringByAppendingPathComponent:@"shaders.metallib"] error:NULL];
  NSOperatingSystemVersion v = NSProcessInfo.processInfo.operatingSystemVersion;
  NSString *name = [NSString stringWithFormat:@"pipelines-%ld.%ld.%ld-%llu-%.0f.metalarchive", (long)v.majorVersion, (long)v.minorVersion, (long)v.patchVersion, shaders.fileSize, shaders.fileModificationDate.timeIntervalSince1970];
  _url = [NSURL fileURLWithPath:[caches stringByAppendingPathComponent:name]];
  MTLBinaryArchiveDescriptor *d = [MTLBinaryArchiveDescriptor new];
  _existing = [NSFileManager.defaultManager fileExistsAtPath:_url.path];
  if (_existing) d.url = _url;
  NSError *error = nil;
  // Apple's shader validation layer crashes when an archive is added to under it: with validation on, pipelines are simply compiled.
  if (getenv("MTL_SHADER_VALIDATION")) return self;
  _archive = [device newBinaryArchiveWithDescriptor:d error:&error];
  if (!_archive && _existing) {        // a damaged file is thrown away, never trusted
    [NSFileManager.defaultManager removeItemAtURL:_url error:NULL];
    _existing = false;
    _archive = [device newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&error];
  }
  return self;
}

- (id<MTLRenderPipelineState>)render:(MTLRenderPipelineDescriptor *)descriptor {
  NSError *error = nil;
  if (_archive) {
    descriptor.binaryArchives = @[_archive];
    // First ask for it from the archive alone; a miss only costs the compile that would have happened anyway.
    id<MTLRenderPipelineState> state = [_device newRenderPipelineStateWithDescriptor:descriptor options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:&error];
    if (state) return state;
    _misses++;
    if ([_archive addRenderPipelineFunctionsWithDescriptor:descriptor error:&error]) _changed = true;
    error = nil;
  }
  id<MTLRenderPipelineState> state = [_device newRenderPipelineStateWithDescriptor:descriptor error:&error];
  if (!state) NSLog(@"pipeline failed: %@", error);
  return state;
}

- (id<MTLComputePipelineState>)compute:(NSString *)function {
  MTLComputePipelineDescriptor *d = [MTLComputePipelineDescriptor new];
  d.computeFunction = [_library newFunctionWithName:function];
  NSError *error = nil;
  if (_archive) {
    d.binaryArchives = @[_archive];
    id<MTLComputePipelineState> state = [_device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:&error];
    if (state) return state;
    _misses++;
    if ([_archive addComputePipelineFunctionsWithDescriptor:d error:&error]) _changed = true;
    error = nil;
  }
  id<MTLComputePipelineState> state = [_device newComputePipelineStateWithDescriptor:d options:0 reflection:nil error:&error];
  if (!state) NSLog(@"pipeline %@ failed: %@", function, error);
  return state;
}

- (void)finish {
  if (!_changed || !_archive) return;
  NSError *error = nil;
  if (![_archive serializeToURL:_url error:&error]) NSLog(@"could not keep the pipelines: %@", error);
  _changed = false;
}
@end
