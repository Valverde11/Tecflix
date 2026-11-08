#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tecflix/users/btree/btree_key.hpp"
#include "tecflix/users/storage/byte_codec.hpp"

namespace tecflix::users {

// Nodo del árbol B tal como vive en memoria mientras se procesa una operación.
// En disco ocupa una página; Serialize/Deserialize lo convierten.
//
// Layout en disco (max_keys = 2t - 1), siempre del mismo tamaño:
//   is_leaf (1 B) | key_count (4 B)
//   keys     [max_keys]     de 32 B   (las no usadas en cero)
//   values   [max_keys]     de 8 B    índice del registro en users.dat
//   children [max_keys + 1] de 8 B    número de página de cada hijo
struct BTreeNode {
  std::uint64_t page = 0;
  bool is_leaf = true;
  std::vector<BTreeKey> keys;
  std::vector<std::uint64_t> values;
  std::vector<std::uint64_t> children;

  static constexpr std::size_t SerializedSize(std::size_t max_keys) {
    return sizeof(std::uint8_t) + sizeof(std::uint32_t) +
           max_keys * (BTreeKey::kSize + sizeof(std::uint64_t)) +
           (max_keys + 1) * sizeof(std::uint64_t);
  }

  // Lanza std::logic_error si el nodo tiene más claves de las permitidas.
  Bytes Serialize(std::size_t max_keys) const;
  // Lanza StorageError si los bytes describen un nodo imposible.
  static BTreeNode Deserialize(std::uint64_t page, const Bytes& bytes,
                               std::size_t max_keys);

  // Posición de la primera clave >= key (búsqueda binaria).
  std::size_t LowerBound(const BTreeKey& key) const;
  bool HasKeyAt(std::size_t index, const BTreeKey& key) const;
};

}  // namespace tecflix::users
