// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "render/mesh_pack.h"
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// The file: "HSM1", part count, vertex count, index count, then the parts, the vertices and the indices, in that order.
bool hs_mesh_pack_load(const char *path, hs_mesh_pack *pack) {
  memset(pack, 0, sizeof *pack);
  int file = open(path, O_RDONLY);
  if (file < 0) return false;
  struct stat info;
  if (fstat(file, &info) != 0 || info.st_size < 16) { close(file); return false; }
  void *mapping = mmap(NULL, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, file, 0);
  close(file);
  if (mapping == MAP_FAILED) return false;
  const uint32_t *header = mapping;
  if (memcmp(mapping, "HSM1", 4) != 0) { munmap(mapping, (size_t)info.st_size); return false; }
  pack->part_count = header[1];
  pack->vertex_count = header[2];
  pack->index_count = header[3];
  size_t needed = 16 + pack->part_count * sizeof(hs_part) + pack->vertex_count * sizeof(hs_vertex) + pack->index_count * sizeof(uint32_t);
  if ((size_t)info.st_size < needed) { munmap(mapping, (size_t)info.st_size); return false; }
  uint8_t *at = (uint8_t *)mapping + 16;
  pack->parts = (hs_part *)at;
  at += pack->part_count * sizeof(hs_part);
  pack->vertices = (hs_vertex *)at;
  at += pack->vertex_count * sizeof(hs_vertex);
  pack->indices = (uint32_t *)at;
  pack->mapping = mapping;
  pack->mapping_size = (size_t)info.st_size;
  return true;
}

int hs_mesh_pack_find(const hs_mesh_pack *pack, const char *name) {
  for (uint32_t i = 0; i < pack->part_count; i++) {
    if (strncmp(pack->parts[i].name, name, HS_PART_NAME) == 0) return (int)i;
  }
  return -1;
}
