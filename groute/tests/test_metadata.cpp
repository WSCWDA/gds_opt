#include <cassert>

#include "groute/metadata.h"

int main() {
  groute::GlobalDirectory dir;

  groute::ChunkMeta m;
  m.key.file_id = 7;
  m.key.chunk_id = 9;
  m.key.generation = 1;
  m.logical_size = 1 << 20;
  m.dio_align = 4096;
  m.reuse_score = 0.5F;
  m.last_access_ns = 1234;
  m.replicas.push_back({groute::LocationKind::LocalSSD_GDS, 0, 0, 0, m.logical_size, 10});

  assert(dir.insert_or_update(m));
  assert(dir.size() == 1);

  auto found = dir.lookup(m.key);
  assert(found.has_value());
  assert(found->key == m.key);
  assert(found->replicas.size() == 1);

  dir.update_reuse_score(m.key, 0.25F);
  auto updated = dir.lookup(m.key);
  assert(updated.has_value());
  assert(updated->reuse_score > 0.74F && updated->reuse_score < 0.76F);

  groute::ChunkKey missing{999, 999, 0};
  auto none = dir.lookup(missing);
  assert(!none.has_value());

  return 0;
}
