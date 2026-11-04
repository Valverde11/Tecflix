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

bool BTree::Remove(const BTreeKey& key) {
  const bool removed = RemoveFrom(pages_.ReadNode(pages_.RootPage()), key);
  ShrinkRootIfEmpty();
  return removed;
}

bool BTree::RemoveFrom(BTreeNode node, const BTreeKey& key) {
  const std::size_t index = node.LowerBound(key);
  if (node.HasKeyAt(index, key)) {
    if (node.is_leaf) {
      // Caso 1: la clave está en una hoja que puede perderla (se garantizó
      // al bajar que tiene al menos t claves, o es la raíz).
      node.keys.erase(At(node.keys, index));
      node.values.erase(At(node.values, index));
      pages_.WriteNode(node);
      return true;
    }
    return RemoveFromInternal(node, index);
  }
  if (node.is_leaf) {
    return false;
  }
  // Caso 3: la clave, si existe, está en el subárbol children[index].
  const std::size_t child_index = EnsureChildCanLoseKey(node, index);
  return RemoveFrom(pages_.ReadNode(node.children[child_index]), key);
}

bool BTree::RemoveFromInternal(BTreeNode& node, std::size_t index) {
  const BTreeKey key = node.keys[index];
  BTreeNode left = pages_.ReadNode(node.children[index]);
  if (left.keys.size() > MinKeys()) {
    // Caso 2a: se reemplaza la clave por su predecesora (la mayor del
    // subárbol izquierdo) y se borra la predecesora de ese subárbol.
    const Entry predecessor = MaxEntry(left);
    node.keys[index] = predecessor.key;
    node.values[index] = predecessor.value;
    pages_.WriteNode(node);
    return RemoveFrom(left, predecessor.key);
  }
  BTreeNode right = pages_.ReadNode(node.children[index + 1]);
  if (right.keys.size() > MinKeys()) {
    // Caso 2b: simétrico, con la sucesora (la menor del subárbol derecho).
    const Entry successor = MinEntry(right);
    node.keys[index] = successor.key;
    node.values[index] = successor.value;
    pages_.WriteNode(node);
    return RemoveFrom(right, successor.key);
  }
  // Caso 2c: ambos hijos tienen el mínimo; se fusionan con la clave en medio
  // y se borra la clave del nodo fusionado.
  MergeChildren(node, index);
  return RemoveFrom(pages_.ReadNode(node.children[index]), key);
}

std::size_t BTree::EnsureChildCanLoseKey(BTreeNode& node, std::size_t index) {
  BTreeNode child = pages_.ReadNode(node.children[index]);
  if (child.keys.size() > MinKeys()) {
    return index;
  }
  const bool has_left = index > 0;
  const bool has_right = index < node.keys.size();
  if (has_left) {
    BTreeNode left = pages_.ReadNode(node.children[index - 1]);
    if (left.keys.size() > MinKeys()) {
      BorrowFromLeft(node, index, child, left);  // caso 3a
      return index;
    }
  }
  if (has_right) {
    BTreeNode right = pages_.ReadNode(node.children[index + 1]);
    if (right.keys.size() > MinKeys()) {
      BorrowFromRight(node, index, child, right);  // caso 3a
      return index;
    }
  }
  // Caso 3b: ningún hermano puede prestar; se fusiona con uno de ellos.
  if (has_right) {
    MergeChildren(node, index);
    return index;
  }
  MergeChildren(node, index - 1);
  return index - 1;
}

// Rotación a la derecha: la clave separadora del padre baja al inicio del
// hijo y la mayor clave del hermano izquierdo sube a ocupar su lugar.
void BTree::BorrowFromLeft(BTreeNode& node, std::size_t index,
                           BTreeNode& child, BTreeNode& left) {
  child.keys.insert(child.keys.begin(), node.keys[index - 1]);
  child.values.insert(child.values.begin(), node.values[index - 1]);
  if (!child.is_leaf) {
    child.children.insert(child.children.begin(), left.children.back());
    left.children.pop_back();
  }
  node.keys[index - 1] = left.keys.back();
  node.values[index - 1] = left.values.back();
  left.keys.pop_back();
  left.values.pop_back();

  pages_.WriteNode(left);
  pages_.WriteNode(child);
  pages_.WriteNode(node);
}

// Rotación a la izquierda: simétrica a BorrowFromLeft.
void BTree::BorrowFromRight(BTreeNode& node, std::size_t index,
                            BTreeNode& child, BTreeNode& right) {
  child.keys.push_back(node.keys[index]);
  child.values.push_back(node.values[index]);
  if (!child.is_leaf) {
    child.children.push_back(right.children.front());
    right.children.erase(right.children.begin());
  }
  node.keys[index] = right.keys.front();
  node.values[index] = right.values.front();
  right.keys.erase(right.keys.begin());
  right.values.erase(right.values.begin());

  pages_.WriteNode(right);
  pages_.WriteNode(child);
  pages_.WriteNode(node);
}

void BTree::MergeChildren(BTreeNode& node, std::size_t index) {
  BTreeNode left = pages_.ReadNode(node.children[index]);
  const BTreeNode right = pages_.ReadNode(node.children[index + 1]);

  // t - 1 claves + la separadora + t - 1 claves = 2t - 1: el resultado cabe.
  left.keys.push_back(node.keys[index]);
  left.values.push_back(node.values[index]);
  left.keys.insert(left.keys.end(), right.keys.begin(), right.keys.end());
  left.values.insert(left.values.end(), right.values.begin(), right.values.end());
  left.children.insert(left.children.end(), right.children.begin(),
                       right.children.end());

  node.keys.erase(At(node.keys, index));
  node.values.erase(At(node.values, index));
  node.children.erase(At(node.children, index + 1));

  pages_.WriteNode(left);
  pages_.WriteNode(node);
  pages_.FreePage(right.page);
}

BTree::Entry BTree::MaxEntry(BTreeNode node) {
  while (!node.is_leaf) {
    node = pages_.ReadNode(node.children.back());
  }
  return {node.keys.back(), node.values.back()};
}

BTree::Entry BTree::MinEntry(BTreeNode node) {
  while (!node.is_leaf) {
    node = pages_.ReadNode(node.children.front());
  }
  return {node.keys.front(), node.values.front()};
}

void BTree::ShrinkRootIfEmpty() {
  const BTreeNode root = pages_.ReadNode(pages_.RootPage());
  if (!root.is_leaf && root.keys.empty()) {
    // Única forma en que el árbol baja de altura: por arriba, igual que crece.
    pages_.SetRootPage(root.children.front());
    pages_.FreePage(root.page);
  }
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
