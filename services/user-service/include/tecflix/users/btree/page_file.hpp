#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "tecflix/users/btree/btree_node.hpp"
#include "tecflix/users/storage/binary_file.hpp"

namespace tecflix::users {

// Archivo del árbol B dividido en páginas de tamaño fijo; cada nodo ocupa una.
//
//   página 0: encabezado (magic, versión, grado, tamaño de clave, raíz,
//             cantidad de páginas, primera página libre)
//   página 1..N: nodos o páginas libres
//
// Como la página 0 nunca es un nodo, el número 0 sirve como "sin página".
// Las páginas que el árbol libera (al fusionar nodos) forman una lista
// enlazada y se reutilizan antes de agrandar el archivo.
class PageFile {
 public:
  static constexpr std::size_t kPageSize = 4096;
  static constexpr std::uint64_t kNoPage = 0;
  static constexpr std::uint32_t kMinDegree = 2;
  // Mayor grado cuyo nodo (2t - 1 claves) cabe en una página.
  static constexpr std::uint32_t kMaxDegree = 32;
  static constexpr std::uint32_t kFormatVersion = 1;
  // "TFBTREE1": identifica el archivo del índice.
  static constexpr std::uint64_t kMagicNumber = 0x3145455254424654ULL;

  // Abre el índice o lo crea con una raíz hoja vacía. Lanza
  // std::invalid_argument si el grado no es válido y StorageError si el
  // archivo existente es inválido o fue creado con otro grado.
  PageFile(const std::filesystem::path& path, std::uint32_t degree);

  std::uint32_t Degree() const { return degree_; }
  std::size_t MaxKeys() const { return 2 * static_cast<std::size_t>(degree_) - 1; }
  std::uint64_t RootPage() const { return root_page_; }
  void SetRootPage(std::uint64_t page);
  std::uint64_t PageCount() const { return page_count_; }

  BTreeNode ReadNode(std::uint64_t page);
  void WriteNode(const BTreeNode& node);
  // Devuelve una página libre (reutilizada o nueva) para un nodo nuevo.
  std::uint64_t AllocatePage();
  // Marca la página como libre y la agrega a la lista de páginas libres.
  void FreePage(std::uint64_t page);

 private:
  void Initialize();
  void LoadHeader();
  void WriteHeader();
  void RequireNodePage(std::uint64_t page) const;
  static std::uint64_t OffsetOf(std::uint64_t page);

  BinaryFile file_;
  std::uint32_t degree_;
  std::uint64_t root_page_ = kNoPage;
  std::uint64_t page_count_ = 0;
  std::uint64_t free_list_head_ = kNoPage;
};

}  // namespace tecflix::users
