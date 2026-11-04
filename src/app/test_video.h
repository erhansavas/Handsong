// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#pragma once
#import <Metal/Metal.h>

bool hs_video_open(const char *path, int width, int height);
void hs_video_add(id<MTLTexture> texture);     // a finished frame at twice the movie's size, in the panel's format
void hs_video_close(void);
