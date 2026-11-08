#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

#include "tecflix/users/btree/btree_key.hpp"
#include "tecflix/users/btree/btree_node.hpp"
#include "tecflix/users/btree/page_file.hpp"

namespace tecflix::users {

// Árbol B en disco que asocia claves únicas con un valor de 64 bits (el índice
// del registro en users.dat). Sigue el algoritmo de Cormen et al.
// (Introduction to Algorithms, cap. 18) con grado mínimo t:
//  - todo nodo tiene como máximo 2t - 1 claves y, salvo la raíz, al menos t - 1;
//  - todas las hojas están a la misma profundidad.
// La inserción divide los nodos llenos *mientras baja*, y el borrado asegura
// que cada nodo al que entra tenga al menos t claves (prestando o fusionando);
// así ninguna operación necesita volver hacia arriba para corregir el árbol.
class BTree {
 public:
  static constexpr std::uint32_t kDefaultDegree = PageFile::kMaxDegree;

  BTree(const std::filesystem::path& path, std::uint32_t degree = kDefaultDegree);

  std::optional<std::uint64_t> Search(const BTreeKey& key);
  // Devuelve false (sin modificar nada) si la clave ya existe.
  bool Insert(const BTreeKey& key, std::uint64_t value);
  // Devuelve false si la clave no existe.
  bool Remove(const BTreeKey& key);

  // Cantidad de niveles: 1 si la raíz es hoja.
  std::size_t Height();
  // Recorre todo el árbol verificando sus invariantes; lanza std::logic_error
  // describiendo la primera violación. Devuelve la cantidad total de claves.
  std::uint64_t CheckInvariants();

 private:
  std::size_t MaxKeys() const;
  std::size_t MinKeys() const;
  bool IsFull(const BTreeNode& node) const;

  void InsertNonFull(BTreeNode node, const BTreeKey& key, std::uint64_t value);
  // Divide el hijo lleno parent.children[index] en dos y sube su clave
  // mediana a parent. Escribe los tres nodos en disco.
  void SplitChild(BTreeNode& parent, std::size_t index, BTreeNode& child);

  struct Entry {
    BTreeKey key;
    std::uint64_t value;
  };
  bool RemoveFrom(BTreeNode node, const BTreeKey& key);
  // La clave está en node.keys[index] y node es interno.
  bool RemoveFromInternal(BTreeNode& node, std::size_t index);
  // Garantiza que node.children[index] tenga al menos t claves antes de bajar
  // a él. Devuelve el índice del hijo donde quedó el rango buscado (cambia si
  // hubo que fusionarlo con su hermano izquierdo).
  std::size_t EnsureChildCanLoseKey(BTreeNode& node, std::size_t index);
  void BorrowFromLeft(BTreeNode& node, std::size_t index, BTreeNode& child,
                      BTreeNode& left);
  void BorrowFromRight(BTreeNode& node, std::size_t index, BTreeNode& child,
                       BTreeNode& right);
  // Une node.children[index], la clave node.keys[index] y
  // node.children[index + 1] en un solo nodo y libera la página del derecho.
  void MergeChildren(BTreeNode& node, std::size_t index);
  Entry MaxEntry(BTreeNode node);
  Entry MinEntry(BTreeNode node);
  // Si la raíz interna quedó sin claves, su único hijo pasa a ser la raíz.
  void ShrinkRootIfEmpty();

  struct KeyRange {
    std::optional<BTreeKey> lower;  // las claves del subárbol son > lower
    std::optional<BTreeKey> upper;  // y < upper
  };
  std::uint64_t CheckNode(std::uint64_t page, const KeyRange& range,
                          std::size_t depth, std::optional<std::size_t>& leaf_depth);

  PageFile pages_;
};

}  // namespace tecflix::users
