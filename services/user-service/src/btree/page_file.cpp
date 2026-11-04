#include "tecflix/users/btree/page_file.hpp"

#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint64_t kHeaderPage = 0;
// Primer byte de una página libre. Un nodo usa 0 (interno) o 1 (hoja), así que
// leer una página libre como nodo se detecta como error y no como datos.
constexpr std::uint8_t kFreePageMarker = 0xFF;

static_assert(BTreeNode::SerializedSize(2 * PageFile::kMaxDegree - 1) <=
                  PageFile::kPageSize,
              "El nodo de grado máximo debe caber en una página");

}  // namespace

PageFile::PageFile(const std::filesystem::path& path, std::uint32_t degree)
    : file_(path), degree_(degree) {
  if (degree < kMinDegree || degree > kMaxDegree) {
    throw std::invalid_argument("Grado del árbol B fuera de rango: " +
                                std::to_string(degree));
  }
  if (file_.WasCreated()) {
    Initialize();
  } else {
    LoadHeader();
  }
}

void PageFile::SetRootPage(std::uint64_t page) {
  RequireNodePage(page);
  root_page_ = page;
  WriteHeader();
}

BTreeNode PageFile::ReadNode(std::uint64_t page) {
  RequireNodePage(page);
  const Bytes bytes =
      file_.ReadAt(OffsetOf(page), BTreeNode::SerializedSize(MaxKeys()));
  if (bytes.front() == kFreePageMarker) {
    throw StorageError("Se intentó leer la página libre " +
                       std::to_string(page) + " como nodo");
  }
  return BTreeNode::Deserialize(page, bytes, MaxKeys());
}

void PageFile::WriteNode(const BTreeNode& node) {
  RequireNodePage(node.page);
  file_.WriteAt(OffsetOf(node.page), node.Serialize(MaxKeys()));
}

std::uint64_t PageFile::AllocatePage() {
  if (free_list_head_ != kNoPage) {
    const std::uint64_t page = free_list_head_;
    ByteReader reader(file_.ReadAt(OffsetOf(page), sizeof(std::uint8_t) +
                                                       sizeof(std::uint64_t)));
    if (reader.ReadUint8() != kFreePageMarker) {
      throw StorageError("Lista de páginas libres corrupta");
    }
    free_list_head_ = reader.ReadUint64();
    WriteHeader();
    return page;
  }
  const std::uint64_t page = page_count_;
  ++page_count_;
  // Se reserva la página completa para que el archivo crezca de a una página.
  file_.WriteAt(OffsetOf(page), Bytes(kPageSize, 0));
  WriteHeader();
  return page;
}

void PageFile::FreePage(std::uint64_t page) {
  RequireNodePage(page);
  if (page == root_page_) {
    throw std::logic_error("No se puede liberar la página raíz");
  }
  ByteWriter writer;
  writer.WriteUint8(kFreePageMarker);
  writer.WriteUint64(free_list_head_);
  file_.WriteAt(OffsetOf(page), writer.Data());
  free_list_head_ = page;
  WriteHeader();
}

void PageFile::Initialize() {
  page_count_ = 1;  // la página 0 es el encabezado
  file_.WriteAt(OffsetOf(kHeaderPage), Bytes(kPageSize, 0));
  BTreeNode root;
  root.page = AllocatePage();
  WriteNode(root);
  root_page_ = root.page;
  WriteHeader();
}

void PageFile::LoadHeader() {
  const std::string name = file_.Path().string();
  if (file_.Size() < kPageSize) {
    throw StorageError("Índice sin encabezado: " + name);
  }
  const Bytes header = file_.ReadAt(OffsetOf(kHeaderPage), kPageSize);
  ByteReader reader(header);
  if (reader.ReadUint64() != kMagicNumber) {
    throw StorageError("No es un índice de árbol B de Tecflix: " + name);
  }
  if (reader.ReadUint32() != kFormatVersion) {
    throw StorageError("Versión de índice no soportada: " + name);
  }
  if (reader.ReadUint32() != degree_) {
    throw StorageError("El índice fue creado con otro grado: " + name);
  }
  if (reader.ReadUint32() != BTreeKey::kSize) {
    throw StorageError("El índice usa otro tamaño de clave: " + name);
  }
  root_page_ = reader.ReadUint64();
  page_count_ = reader.ReadUint64();
  free_list_head_ = reader.ReadUint64();
  if (file_.Size() < OffsetOf(page_count_)) {
    throw StorageError("Índice truncado: " + name);
  }
  RequireNodePage(root_page_);
}

void PageFile::WriteHeader() {
  ByteWriter writer;
  writer.WriteUint64(kMagicNumber);
  writer.WriteUint32(kFormatVersion);
  writer.WriteUint32(degree_);
  writer.WriteUint32(static_cast<std::uint32_t>(BTreeKey::kSize));
  writer.WriteUint64(root_page_);
  writer.WriteUint64(page_count_);
  writer.WriteUint64(free_list_head_);
  file_.WriteAt(OffsetOf(kHeaderPage), writer.Data());
}

void PageFile::RequireNodePage(std::uint64_t page) const {
  if (page == kHeaderPage || page >= page_count_) {
    throw StorageError("Número de página inválido: " + std::to_string(page));
  }
}

std::uint64_t PageFile::OffsetOf(std::uint64_t page) {
  return page * kPageSize;
}

}  // namespace tecflix::users
