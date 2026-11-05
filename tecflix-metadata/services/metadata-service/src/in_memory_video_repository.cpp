#include "tecflix/metadata/in_memory_video_repository.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <vector>

namespace tecflix::metadata {
namespace {

std::string LowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool Matches(const Video& video, const VideoQuery& query) {
  if (!IsVisibleTo(video, query.viewer_id)) return false;
  if (query.only_mine && video.owner_id != query.viewer_id) return false;
  if (query.tag &&
      std::find(video.tags.begin(), video.tags.end(), *query.tag) == video.tags.end()) {
    return false;
  }
  if (query.text &&
      LowerAscii(video.title).find(LowerAscii(*query.text)) == std::string::npos) {
    return false;
  }
  return true;
}

// Orden total: ante empates se desempata por id, así la paginación es estable.
bool Precedes(const Video& left, const Video& right, SortField field) {
  switch (field) {
    case SortField::kTitle: {
      const auto a = LowerAscii(left.title);
      const auto b = LowerAscii(right.title);
      if (a != b) return a < b;
      break;
    }
    case SortField::kAuthor: {
      const auto a = LowerAscii(left.owner_name);
      const auto b = LowerAscii(right.owner_name);
      if (a != b) return a < b;
      break;
    }
    case SortField::kUploadedAt:
      if (left.uploaded_at != right.uploaded_at) return left.uploaded_at < right.uploaded_at;
      break;
  }
  return left.id < right.id;
}

}  // namespace

bool InMemoryVideoRepository::Insert(const Video& video) {
  const std::lock_guard<std::mutex> lock(mutex_);
  return videos_.emplace(video.id, video).second;
}

std::optional<Video> InMemoryVideoRepository::FindById(const std::string& id) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto found = videos_.find(id);
  if (found == videos_.end()) return std::nullopt;
  return found->second;
}

bool InMemoryVideoRepository::Replace(const Video& video) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto found = videos_.find(video.id);
  if (found == videos_.end()) return false;
  found->second = video;
  return true;
}

bool InMemoryVideoRepository::Delete(const std::string& id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  return videos_.erase(id) > 0;
}

VideoPage InMemoryVideoRepository::List(const VideoQuery& query) const {
  std::vector<Video> matching;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : videos_) {
      if (Matches(entry.second, query)) matching.push_back(entry.second);
    }
  }
  std::sort(matching.begin(), matching.end(), [&query](const Video& a, const Video& b) {
    return query.ascending ? Precedes(a, b, query.sort) : Precedes(b, a, query.sort);
  });

  VideoPage page;
  page.total = static_cast<std::int64_t>(matching.size());
  const auto page_size = static_cast<std::size_t>(query.page_size);
  const auto offset = static_cast<std::size_t>(query.page - 1) * page_size;
  if (offset < matching.size()) {
    const auto end = std::min(matching.size(), offset + page_size);
    page.items.assign(matching.begin() + static_cast<std::ptrdiff_t>(offset),
                      matching.begin() + static_cast<std::ptrdiff_t>(end));
  }
  return page;
}

UsageSummary InMemoryVideoRepository::Usage(const std::string& owner_id) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  UsageSummary usage;
  for (const auto& entry : videos_) {
    if (entry.second.owner_id != owner_id) continue;
    ++usage.video_count;
    usage.used_bytes += entry.second.size_bytes;
  }
  return usage;
}

}  // namespace tecflix::metadata
