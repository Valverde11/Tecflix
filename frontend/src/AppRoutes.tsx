import { Navigate, Route, Routes } from 'react-router';
import { LoginPage } from './features/auth/LoginPage';
import { RegisterPage } from './features/auth/RegisterPage';
import { VideoDetailPage } from './features/player/VideoDetailPage';
import { UploadPage } from './features/upload/UploadPage';
import { VideoListPage } from './features/videos/VideoListPage';

export function AppRoutes() {
  return (
    <Routes>
      <Route path="/" element={<Navigate to="/login" replace />} />
      <Route path="/login" element={<LoginPage />} />
      <Route path="/register" element={<RegisterPage />} />
      <Route path="/videos" element={<VideoListPage />} />
      <Route path="/videos/:id" element={<VideoDetailPage />} />
      <Route path="/upload" element={<UploadPage />} />
      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  );
}
