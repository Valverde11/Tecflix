#include "tecflix/users/btree/btree.hpp"

#include <stdexcept>
#include <string>

namespace tecflix::users {
namespace {

// Desplazamiento para convertir un índice de vector en iterador.
template <typename T>
auto At(std::vector<T>& items, std::size_t index) {
  return items.begin() + static_cast<std::ptrdiff_t>(index);
}

}  // namespace

BTree::BTree(const std::filesystem::path& path, std::uint32_t degree)
    : pages_(path, degree) {}

std::optional<std::uint64_t> BTree::Search(const BTreeKey& key) {
  BTreeNode node = pages_.ReadNode(pages_.RootPage());
  while (true) {
    const std::size_t index = node.LowerBound(key);
    if (node.HasKeyAt(index, key)) {
      return node.values[index];
    }
    if (node.is_leaf) {
      return std::nullopt;
    }
    node = pages_.ReadNode(node.children[index]);
  }
}

bool BTree::Insert(const BTreeKey& key, std::uint64_t value) {
  // Se verifica antes de modificar: si la clave existe no debe dividirse ningún
  // nodo en el camino, para no cambiar el árbol en una operación rechazada.
  if (Search(key)) {
    return false;
  }
  BTreeNode root = pages_.ReadNode(pages_.RootPage());
  if (IsFull(root)) {
    // Única forma en que el árbol crece en altura: por arriba, con una raíz
    // nueva. Por eso todas las hojas quedan siempre a la misma profundidad.
    BTreeNode new_root;
    new_root.page = pages_.AllocatePage();
    new_root.is_leaf = false;
    new_root.children.push_back(root.page);
    SplitChild(new_root, 0, root);
    pages_.SetRootPage(new_root.page);
    InsertNonFull(new_root, key, value);
  } else {
    InsertNonFull(root, key, value);
  }
  return true;
}

void BTree::InsertNonFull(BTreeNode node, const BTreeKey& key,
                          std::uint64_t value) {
  while (true) {
    std::size_t index = node.LowerBound(key);
    if (node.is_leaf) {
      node.keys.insert(At(node.keys, index), key);
      node.values.insert(At(node.values, index), value);
      pages_.WriteNode(node);
      return;
    }
    BTreeNode child = pages_.ReadNode(node.children[index]);
    if (IsFull(child)) {
      SplitChild(node, index, child);
      // La mediana subió a node.keys[index]: decidir a cuál mitad bajar.
      if (node.keys[index] < key) {
        ++index;
      }
      child = pages_.ReadNode(node.children[index]);
    }
    node = child;
  }
}

void BTree::SplitChild(BTreeNode& parent, std::size_t index, BTreeNode& child) {
  const std::size_t median = MinKeys();  // posición t - 1

  BTreeNode sibling;
  sibling.page = pages_.AllocatePage();
  sibling.is_leaf = child.is_leaf;
  sibling.keys.assign(At(child.keys, median + 1), child.keys.end());
  sibling.values.assign(At(child.values, median + 1), child.values.end());
  if (!child.is_leaf) {
    sibling.children.assign(At(child.children, median + 1),
                            child.children.end());
    child.children.resize(median + 1);
  }

  parent.keys.insert(At(parent.keys, index), child.keys[median]);
  parent.values.insert(At(parent.values, index), child.values[median]);
  parent.children.insert(At(parent.children, index + 1), sibling.page);

  child.keys.resize(median);
  child.values.resize(median);

  pages_.WriteNode(child);
  pages_.WriteNode(sibling);
  pages_.WriteNode(parent);
}

std::size_t BTree::Height() {
  std::size_t height = 1;
  BTreeNode node = pages_.ReadNode(pages_.RootPage());
  while (!node.is_leaf) {
    node = pages_.ReadNode(node.children.front());
    ++height;
  }
  return height;
}

std::uint64_t BTree::CheckInvariants() {
  std::optional<std::size_t> leaf_depth;
  return CheckNode(pages_.RootPage(), KeyRange{}, 0, leaf_depth);
}

std::uint64_t BTree::CheckNode(std::uint64_t page, const KeyRange& range,
                               std::size_t depth,
                               std::optional<std::size_t>& leaf_depth) {
  const BTreeNode node = pages_.ReadNode(page);
  const std::string where = " (página " + std::to_string(page) + ")";
  const bool is_root = page == pages_.RootPage();

  if (node.keys.size() > MaxKeys()) {
    throw std::logic_error("Nodo con demasiadas claves" + where);
  }
  if (!is_root && node.keys.size() < MinKeys()) {
    throw std::logic_error("Nodo con menos claves que el mínimo" + where);
  }
  if (is_root && !node.is_leaf && node.keys.empty()) {
    throw std::logic_error("Raíz interna sin claves" + where);
  }
  for (std::size_t i = 0; i < node.keys.size(); ++i) {
    if (i > 0 && !(node.keys[i - 1] < node.keys[i])) {
      throw std::logic_error("Claves desordenadas o repetidas" + where);
    }
    if ((range.lower && !(*range.lower < node.keys[i])) ||
        (range.upper && !(node.keys[i] < *range.upper))) {
      throw std::logic_error("Clave fuera del rango de su subárbol" + where);
    }
  }

  std::uint64_t key_count = node.keys.size();
  if (node.is_leaf) {
    if (leaf_depth && *leaf_depth != depth) {
      throw std::logic_error("Hojas a distinta profundidad" + where);
    }
    leaf_depth = depth;
    return key_count;
  }
  if (node.children.size() != node.keys.size() + 1) {
    throw std::logic_error("Cantidad de hijos incorrecta" + where);
  }
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    KeyRange child_range;
    child_range.lower = i == 0 ? range.lower : node.keys[i - 1];
    child_range.upper = i == node.keys.size() ? range.upper : node.keys[i];
    key_count += CheckNode(node.children[i], child_range, depth + 1, leaf_depth);
  }
  return key_count;
}

std::size_t BTree::MaxKeys() const { return pages_.MaxKeys(); }

std::size_t BTree::MinKeys() const { return pages_.Degree() - 1; }

bool BTree::IsFull(const BTreeNode& node) const {
  return node.keys.size() == MaxKeys();
}

}  // namespace tecflix::users
