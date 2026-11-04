// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// A model pack: named parts ready for the GPU, with what the rig needs to move them. The default pack is the instrument, and
// the app also accepts another pack in the user's data folder, as long as it follows the same rig.
#pragma once
#include "../../shaders/shared.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { HS_PART_NAME = 32 };

typedef enum { HS_PASS_SOLID, HS_PASS_CLEAR, HS_PASS_CLEAR_BACK, HS_PASS_DOME, HS_PASS_DOME_BACK } hs_part_pass;

typedef struct {
  char name[HS_PART_NAME];
  uint32_t vertex_count, index_count, vertex_offset, index_offset;   // into the pack's two arrays, in elements
  int32_t material, pass;
  float pivot[3], axis[3], travel[2];     // where it turns or slides, along what, and how far in its own unit
  int32_t parent;                         // a part it rides on, or -1
  float bounds_min[3], bounds_max[3];
} hs_part;

typedef struct {
  uint32_t part_count, vertex_count, index_count;
  hs_part *parts;
  hs_vertex *vertices;
  uint32_t *indices;
  void *mapping;
  size_t mapping_size;
} hs_mesh_pack;

bool hs_mesh_pack_load(const char *path, hs_mesh_pack *pack);
int hs_mesh_pack_find(const hs_mesh_pack *pack, const char *name);
