// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// handsong --test render: draws frames with no window, for the picture gates.
//   --out path.png   --size WxH   --hold pitch,yaw,roll   --eye x,y,z,tx,ty,tz,fov   --frames N  --sequence yaw_step
//   --gpu-cost seconds            draws as fast as the GPU allows and reports the true cost of a frame
//   --press key=value,...         moves controls: trigger, sticks, keys, cross, shoulders, dome
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#import "app/test_modes.h"
#import "render/renderer.h"
#include "world/crew.h"
#include "world/world.h"
#import <Foundation/Foundation.h>
#include <mach/mach_time.h>

static simd_float4x4 attitude(float pitch, float yaw, float roll) {
  const float k = (float)M_PI / 180;
  simd_quatf q = simd_mul(simd_quaternion(yaw * k, simd_make_float3(0, 1, 0)),
                          simd_mul(simd_quaternion(pitch * k, simd_make_float3(1, 0, 0)), simd_quaternion(roll * k, simd_make_float3(0, 0, 1))));
  return simd_matrix4x4(q);
}

static void apply_press(hs_scene *s, const char *text) {
  NSArray *items = [@(text) componentsSeparatedByString:@","];
  for (NSString *item in items) {
    NSArray *pair = [item componentsSeparatedByString:@"="];
    if (pair.count != 2) continue;
    NSString *k = pair[0];
    float v = [pair[1] floatValue];
    if ([k isEqual:@"trigger"]) s->triggers[0] = s->triggers[1] = v;
    else if ([k isEqual:@"sticks"]) { s->sticks[0][0] = v; s->sticks[1][1] = v; }
    else if ([k isEqual:@"pills"]) s->pills[0] = s->pills[1] = s->mute = v;
    else if ([k isEqual:@"keys"]) s->keys[0] = s->keys[1] = s->keys[2] = s->keys[3] = v;
    else if ([k isEqual:@"cross"]) s->cross[0] = v;
    else if ([k isEqual:@"shoulders"]) s->shoulders[0] = s->shoulders[1] = v;
    else if ([k isEqual:@"gear"]) s->gear_turn[0] = s->gear_turn[1] = v;
  }
}

int hs_test_render(int argc, char **argv) {
  NSString *out = nil;
  int width = 2560, height = 1664, frames = 1;
  float yaw_step = 0;
  double gpu_cost = 0, crew_seconds = 0;
  int lattice = 0;
  hs_scene scene = hs_scene_default();
  float hold[3] = {0, 0, 0};
  for (int i = 0; i + 1 < argc; i += 2) {
    const char *k = argv[i], *v = argv[i + 1];
    if (!strcmp(k, "--out")) out = @(v);
    else if (!strcmp(k, "--size")) sscanf(v, "%dx%d", &width, &height);
    else if (!strcmp(k, "--hold")) sscanf(v, "%f,%f,%f", &hold[0], &hold[1], &hold[2]);
    else if (!strcmp(k, "--eye")) {
      float e[7];
      if (sscanf(v, "%f,%f,%f,%f,%f,%f,%f", &e[0], &e[1], &e[2], &e[3], &e[4], &e[5], &e[6]) == 7) {
        scene.eye = simd_make_float3(e[0], e[1], e[2]);
        scene.target = simd_make_float3(e[3], e[4], e[5]);
        scene.fov_degrees = e[6];
      }
    } else if (!strcmp(k, "--frames")) frames = atoi(v);
    else if (!strcmp(k, "--sequence")) yaw_step = (float)atof(v);
    else if (!strcmp(k, "--gpu-cost")) gpu_cost = atof(v);
    else if (!strcmp(k, "--press")) apply_press(&scene, v);
    else if (!strcmp(k, "--exposure")) scene.exposure = (float)atof(v);
    else if (!strcmp(k, "--crew")) crew_seconds = atof(v);
    else if (!strcmp(k, "--lattice")) lattice = atoi(v);
  }
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  NSString *resources = NSBundle.mainBundle.resourcePath;
  static hs_mesh_pack pack;
  NSString *model = [resources stringByAppendingPathComponent:@"instrument.hsmesh"];
  if (getenv("HANDSONG_MODEL")) model = @(getenv("HANDSONG_MODEL"));
  if (!hs_mesh_pack_load(model.fileSystemRepresentation, &pack)) { fprintf(stderr, "no model at %s\n", model.UTF8String); return 1; }
  double started = CACurrentMediaTime();
  HSRenderer *renderer = [[HSRenderer alloc] initWithDevice:device resources:resources meshPack:&pack];
  if (!renderer) return 1;
  if (getenv("HANDSONG_REPORT_LOAD")) {
    printf("renderer ready in %.0f ms\n", (CACurrentMediaTime() - started) * 1000);
    hs_scene first = hs_scene_default();
    [renderer draw:&first into:nil size:CGSizeMake(width, height)];
    [renderer waitUntilIdle];
    // From the moment the system started this process, to a frame finished by the GPU.
    struct kinfo_proc info;
    size_t length = sizeof info;
    int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    sysctl(name, 4, &info, &length, NULL, 0);
    struct timeval now;
    gettimeofday(&now, NULL);
    printf("first frame finished %.0f ms after the process began\n", ((double)(now.tv_sec - info.kp_proc.p_starttime.tv_sec) + (double)(now.tv_usec - info.kp_proc.p_starttime.tv_usec) * 1e-6) * 1000);
  }
  CGSize size = CGSizeMake(width, height);
  // The lattice is a clear conductor: it shows only while it wakes, in its own chapter.
  static uint8_t hidden[HS_MAX_PART_POSES];
  for (const char *const *name = (const char *const[]){"lattice", "lattice_nodes", NULL}; *name; name++) {
    int at = hs_mesh_pack_find(&pack, *name);
    if (at >= 0) hidden[at] = !lattice;
  }
  scene.part_hidden = hidden;
  scene.object = attitude(hold[0], hold[1], hold[2]);
  if (crew_seconds > 0) {       // the crew, after some time at work, as they would stand in that frame
    hs_world_create();
    hs_crew_init();
    hs_crew_set_awake(1);
    const float felt[3] = {0, 1, 0}, turn[3] = {0, 0, 0};
    int steps = (int)(crew_seconds * 750);
    for (int i = 0; i < steps; i++) {
      hs_crew_step(felt, 1.0f / 750, i / 0.75);
      hs_world_step(felt, turn, 1.0f / 750);
      if (i % 12 == 0) hs_crew_pose(&scene, scene.eye, 1.0f / 60), scene.capsule_count = 0;
    }
    hs_crew_pose(&scene, scene.eye, 1.0f / 60);
  }
  if (gpu_cost > 0) {
    double start = CACurrentMediaTime(), sum = 0;
    long n = 0;
    while (CACurrentMediaTime() - start < gpu_cost) {
      [renderer draw:&scene into:nil size:size];
      if (n > 20) sum += renderer.lastGpuSeconds;
      n++;
    }
    [renderer waitUntilIdle];
    printf("frames %ld, GPU per frame %.2f ms at full clock\n%s\n", n, sum / MAX(n - 21, 1) * 1000, renderer.passReport.UTF8String);
    return 0;
  }
  for (int f = 0; f < frames; f++) {
    scene.object = attitude(hold[0], hold[1] + yaw_step * (float)f, hold[2]);
    if (out) {
      NSString *path = frames > 1 ? [NSString stringWithFormat:@"%@_%d.%@", out.stringByDeletingPathExtension, f, out.pathExtension] : out;
      [renderer saveNextFrameTo:path];
    }
    [renderer draw:&scene into:nil size:size];
    [renderer waitUntilIdle];
  }
  return 0;
}
