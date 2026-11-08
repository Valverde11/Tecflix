#include "tecflix/metadata/catalog_service.hpp"

#include <utility>

#include "tecflix/metadata/catalog_error.hpp"
#include "tecflix/metadata/video_validation.hpp"

namespace tecflix::metadata {
namespace {

std::optional<std::string> ViewerId(const std::optional<Identity>& viewer) {
  if (!viewer) return std::nullopt;
  return viewer->id;
}

void ApplyPatch(const VideoPatch& patch, Video& video) {
  if (patch.title) video.title = *patch.title;
  if (patch.description) video.description = *patch.description;
  if (patch.topic) video.topic = *patch.topic;
  if (patch.tags) video.tags = *patch.tags;
  if (patch.duration_seconds) video.duration_seconds = *patch.duration_seconds;
  if (patch.visibility) video.visibility = *patch.visibility;
  if (patch.genres) video.genres = *patch.genres;
  if (patch.release_year) video.release_year = *patch.release_year;
  if (patch.cast) video.cast = *patch.cast;
}

}  // namespace

CatalogService::CatalogService(VideoRepository& repository, Clock clock)
    : repository_(repository), clock_(std::move(clock)) {}

Video CatalogService::Create(const Identity& user, const CreateVideoRequest& request) {
  Video video;
  video.id = request.id;
  video.owner_id = user.id;
  // Sin nombre explícito se usa el id: el orden por autor sigue siendo determinista.
  video.owner_name = user.name.empty() ? user.id : user.name;
  video.title = request.title;
  video.description = request.description;
  video.topic = request.topic;
  video.tags = request.tags;
  video.duration_seconds = request.duration_seconds;
  video.size_bytes = request.size_bytes;
  video.visibility = request.visibility;
  video.genres = request.genres;
  video.release_year = request.release_year;
  video.cast = request.cast;
  video.uploaded_at = clock_();
  video.updated_at = video.uploaded_at;

  ValidateVideo(video);
  if (!repository_.Insert(video)) throw CatalogError::Conflict();
  return video;
}

Video CatalogService::Get(const std::optional<Identity>& viewer,
                          const std::string& id) const {
  if (!IsValidVideoId(id)) throw CatalogError::NotFound();
  const auto video = repository_.FindById(id);
  if (!video || !IsVisibleTo(*video, ViewerId(viewer))) throw CatalogError::NotFound();
  return *video;
}

Video CatalogService::LoadOwned(const Identity& user, const std::string& id) const {
  if (!IsValidVideoId(id)) throw CatalogError::NotFound();
  const auto video = repository_.FindById(id);
  if (!video || !IsVisibleTo(*video, user.id)) throw CatalogError::NotFound();
  if (video->owner_id != user.id) throw CatalogError::Forbidden();
  return *video;
}

Video CatalogService::Update(const Identity& user, const std::string& id,
                             const VideoPatch& patch) {
  Video video = LoadOwned(user, id);
  ApplyPatch(patch, video);
  video.updated_at = clock_();
  ValidateVideo(video);
  if (!repository_.Replace(video)) throw CatalogError::NotFound();
  return video;
}

void CatalogService::Delete(const Identity& user, const std::string& id) {
  const Video video = LoadOwned(user, id);
  if (!repository_.Delete(video.id)) throw CatalogError::NotFound();
}

VideoPage CatalogService::List(const std::optional<Identity>& viewer,
                               VideoQuery query) const {
  if (query.only_mine && !viewer) throw CatalogError::Unauthorized();
  query.viewer_id = ViewerId(viewer);
  return repository_.List(query);
}

UsageSummary CatalogService::Usage(const Identity& user, const std::string& owner_id) const {
  if (user.id != owner_id) throw CatalogError::Forbidden();
  return repository_.Usage(owner_id);
}

}  // namespace tecflix::metadata
