#include "tecflix/users/btree/btree_node.hpp"

#include <algorithm>
#include <stdexcept>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint64_t kUnusedSlot = 0;

}  // namespace

Bytes BTreeNode::Serialize(std::size_t max_keys) const {
  if (keys.size() > max_keys || values.size() != keys.size() ||
      children.size() > max_keys + 1) {
    throw std::logic_error("Nodo del árbol B con tamaño inválido");
  }
  ByteWriter writer;
  writer.WriteUint8(is_leaf ? 1 : 0);
  writer.WriteUint32(static_cast<std::uint32_t>(keys.size()));
  for (std::size_t i = 0; i < max_keys; ++i) {
    writer.WriteBytes(i < keys.size() ? keys[i].Bytes() : BTreeKey::Storage{});
  }
  for (std::size_t i = 0; i < max_keys; ++i) {
    writer.WriteUint64(i < values.size() ? values[i] : kUnusedSlot);
  }
  for (std::size_t i = 0; i <= max_keys; ++i) {
    writer.WriteUint64(i < children.size() ? children[i] : kUnusedSlot);
  }
  return writer.Data();
}

BTreeNode BTreeNode::Deserialize(std::uint64_t page, const Bytes& bytes,
                                 std::size_t max_keys) {
  ByteReader reader(bytes);
  BTreeNode node;
  node.page = page;
  node.is_leaf = reader.ReadUint8() == 1;
  const std::size_t key_count = reader.ReadUint32();
  if (key_count > max_keys) {
    throw StorageError("Nodo del árbol B corrupto en la página " +
                       std::to_string(page));
  }
  for (std::size_t i = 0; i < max_keys; ++i) {
    const BTreeKey key(reader.ReadBytes<BTreeKey::kSize>());
    if (i < key_count) {
      node.keys.push_back(key);
    }
  }
  for (std::size_t i = 0; i < max_keys; ++i) {
    const std::uint64_t value = reader.ReadUint64();
    if (i < key_count) {
      node.values.push_back(value);
    }
  }
  // Una hoja no tiene hijos; un nodo interno tiene exactamente key_count + 1.
  const std::size_t child_count = node.is_leaf ? 0 : key_count + 1;
  for (std::size_t i = 0; i <= max_keys; ++i) {
    const std::uint64_t child = reader.ReadUint64();
    if (i < child_count) {
      node.children.push_back(child);
    }
  }
  return node;
}

std::size_t BTreeNode::LowerBound(const BTreeKey& key) const {
  return static_cast<std::size_t>(
      std::lower_bound(keys.begin(), keys.end(), key) - keys.begin());
}

bool BTreeNode::HasKeyAt(std::size_t index, const BTreeKey& key) const {
  return index < keys.size() && keys[index] == key;
}

}  // namespace tecflix::users
