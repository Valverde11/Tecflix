#include "tecflix/metadata/video.hpp"

namespace tecflix::metadata {

std::string VisibilityToString(Visibility visibility) {
  return visibility == Visibility::kPublic ? "public" : "private";
}

std::optional<Visibility> VisibilityFromString(std::string_view text) {
  if (text == "public") return Visibility::kPublic;
  if (text == "private") return Visibility::kPrivate;
  return std::nullopt;
}

bool IsVisibleTo(const Video& video, const std::optional<std::string>& viewer_id) {
  return video.visibility == Visibility::kPublic ||
         (viewer_id.has_value() && *viewer_id == video.owner_id);
}

}  // namespace tecflix::metadata
