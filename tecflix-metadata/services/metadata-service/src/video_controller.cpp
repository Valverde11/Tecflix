#include "tecflix/metadata/video_controller.hpp"

#include <charconv>
#include <exception>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "tecflix/metadata/catalog_error.hpp"
#include "tecflix/metadata/video_json.hpp"

namespace tecflix::metadata {
namespace {

constexpr int kDefaultPageSize = 20;
constexpr int kMaxPageSize = 100;
constexpr int kMaxPage = 1000000;
constexpr std::size_t kMaxHeaderValueLength = 128;
constexpr std::size_t kMaxFilterLength = 100;
constexpr char kJsonContentType[] = "application/json";

void WriteJson(httplib::Response& response, int status, const nlohmann::json& body) {
  response.status = status;
  response.set_content(body.dump(), kJsonContentType);
}

// Formato de error del contrato común: {"error":"codigo_snake_case","message":"..."}.
void WriteError(httplib::Response& response, int status, const std::string& code,
                const std::string& message) {
  WriteJson(response, status, {{"error", code}, {"message", message}});
}

[[noreturn]] void FailQuery(const std::string& message) {
  throw CatalogError::Invalid("invalid_query", message);
}

bool IsValidHeaderValue(const std::string& value) {
  if (value.size() > kMaxHeaderValueLength) return false;
  for (const char c : value) {
    if (static_cast<unsigned char>(c) < 0x20) return false;
  }
  return true;
}

// Lee la identidad provisional (X-User-Id / X-User-Name). std::nullopt si no hay usuario.
std::optional<Identity> ReadIdentity(const httplib::Request& request) {
  const std::string id = request.get_header_value(VideoController::kUserIdHeader);
  if (id.empty()) return std::nullopt;
  const std::string name = request.get_header_value(VideoController::kUserNameHeader);
  if (!IsValidHeaderValue(id) || !IsValidHeaderValue(name)) {
    throw CatalogError::Invalid("invalid_user_header",
                                "X-User-Id o X-User-Name tienen un valor inválido");
  }
  return Identity{id, name};
}

Identity RequireIdentity(const httplib::Request& request) {
  auto identity = ReadIdentity(request);
  if (!identity) throw CatalogError::Unauthorized();
  return *identity;
}

nlohmann::json ParseBody(const httplib::Request& request) {
  const std::string content_type = request.get_header_value("Content-Type");
  if (content_type.rfind(kJsonContentType, 0) != 0) throw CatalogError::UnsupportedMediaType();
  auto body = nlohmann::json::parse(request.body, nullptr, /*allow_exceptions=*/false);
  if (body.is_discarded()) {
    throw CatalogError::Invalid("invalid_json", "El cuerpo no es JSON válido");
  }
  return body;
}

int ParseIntParam(const httplib::Request& request, const char* name, int fallback, int min,
                  int max) {
  if (!request.has_param(name)) return fallback;
  const std::string text = request.get_param_value(name);
  int value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc() || result.ptr != text.data() + text.size() || value < min ||
      value > max) {
    FailQuery(std::string(name) + " debe ser un entero entre " + std::to_string(min) +
              " y " + std::to_string(max));
  }
  return value;
}

std::optional<std::string> ParseFilterParam(const httplib::Request& request, const char* name) {
  if (!request.has_param(name)) return std::nullopt;
  std::string text = request.get_param_value(name);
  if (text.empty() || text.size() > kMaxFilterLength) {
    FailQuery(std::string(name) + " no puede estar vacío ni pasar de 100 caracteres");
  }
  return text;
}

SortField ParseSort(const httplib::Request& request) {
  if (!request.has_param("sort")) return SortField::kUploadedAt;
  const std::string text = request.get_param_value("sort");
  if (text == "title") return SortField::kTitle;
  if (text == "author") return SortField::kAuthor;
  if (text == "uploaded_at") return SortField::kUploadedAt;
  FailQuery("sort debe ser title, author o uploaded_at");
}

// Sin "order": fechas de la más nueva a la más vieja; texto de la A a la Z.
bool ParseAscending(const httplib::Request& request, SortField sort) {
  if (!request.has_param("order")) return sort != SortField::kUploadedAt;
  const std::string text = request.get_param_value("order");
  if (text == "asc") return true;
  if (text == "desc") return false;
  FailQuery("order debe ser asc o desc");
}

bool ParseMine(const httplib::Request& request) {
  if (!request.has_param("mine")) return false;
  const std::string text = request.get_param_value("mine");
  if (text == "true") return true;
  if (text == "false") return false;
  FailQuery("mine debe ser true o false");
}

VideoQuery ParseListQuery(const httplib::Request& request) {
  VideoQuery query;
  query.page = ParseIntParam(request, "page", 1, 1, kMaxPage);
  query.page_size = ParseIntParam(request, "page_size", kDefaultPageSize, 1, kMaxPageSize);
  query.sort = ParseSort(request);
  query.ascending = ParseAscending(request, query.sort);
  query.only_mine = ParseMine(request);
  query.tag = ParseFilterParam(request, "tag");
  query.text = ParseFilterParam(request, "q");
  return query;
}

}  // namespace

VideoController::VideoController(CatalogService& service) : service_(service) {}

void VideoController::RegisterRoutes(httplib::Server& server) const {
  server.Post(kVideosRoute, Wrap(&VideoController::Create));
  server.Get(kVideosRoute, Wrap(&VideoController::List));
  server.Get(kVideoRoute, Wrap(&VideoController::Get));
  server.Patch(kVideoRoute, Wrap(&VideoController::Patch));
  server.Delete(kVideoRoute, Wrap(&VideoController::Delete));
  server.Get(kUsageRoute, Wrap(&VideoController::Usage));
}

httplib::Server::Handler VideoController::Wrap(Method method) const {
  return [this, method](const httplib::Request& request, httplib::Response& response) {
    try {
      (this->*method)(request, response);
    } catch (const CatalogError& error) {
      WriteError(response, error.HttpStatus(), error.code(), error.what());
    } catch (const std::exception& error) {
      // Fallo del servidor (por ejemplo MongoDB caído): se registra, pero no se filtra
      // el detalle interno al cliente.
      std::cerr << "metadata-service: error no controlado en " << request.method << ' '
                << request.path << ": " << error.what() << '\n';
      WriteError(response, 500, "internal_error", "Error interno del servidor");
    }
  };
}

void VideoController::Create(const httplib::Request& request,
                             httplib::Response& response) const {
  const Identity user = RequireIdentity(request);
  const auto body = ParseBody(request);
  const Video video = service_.Create(user, ParseCreateRequest(body));
  response.set_header("Location", std::string(kVideosRoute) + "/" + video.id);
  WriteJson(response, 201, VideoToJson(video));
}

void VideoController::List(const httplib::Request& request,
                           httplib::Response& response) const {
  const auto viewer = ReadIdentity(request);
  const VideoQuery query = ParseListQuery(request);
  const VideoPage page = service_.List(viewer, query);

  nlohmann::json items = nlohmann::json::array();
  for (const auto& video : page.items) items.push_back(VideoToJson(video));
  WriteJson(response, 200,
            {{"items", items},
             {"page", query.page},
             {"page_size", query.page_size},
             {"total", page.total}});
}

void VideoController::Get(const httplib::Request& request,
                          httplib::Response& response) const {
  const auto viewer = ReadIdentity(request);
  WriteJson(response, 200,
            VideoToJson(service_.Get(viewer, request.path_params.at("id"))));
}

void VideoController::Patch(const httplib::Request& request,
                            httplib::Response& response) const {
  const Identity user = RequireIdentity(request);
  const auto body = ParseBody(request);
  const Video video =
      service_.Update(user, request.path_params.at("id"), ParsePatchRequest(body));
  WriteJson(response, 200, VideoToJson(video));
}

void VideoController::Delete(const httplib::Request& request,
                             httplib::Response& response) const {
  const Identity user = RequireIdentity(request);
  service_.Delete(user, request.path_params.at("id"));
  response.status = 204;
}

void VideoController::Usage(const httplib::Request& request,
                            httplib::Response& response) const {
  const Identity user = RequireIdentity(request);
  const std::string& owner_id = request.path_params.at("owner_id");
  const UsageSummary usage = service_.Usage(user, owner_id);
  WriteJson(response, 200,
            {{"owner_id", owner_id},
             {"video_count", usage.video_count},
             {"used_bytes", usage.used_bytes}});
}

}  // namespace tecflix::metadata
