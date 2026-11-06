import { Navigate, Route, Routes } from 'react-router';
import { AppLayout } from './components/AppLayout';
import { LoginPage } from './features/auth/LoginPage';
import { RedirectIfAuthenticated } from './features/auth/RedirectIfAuthenticated';
import { RegisterPage } from './features/auth/RegisterPage';
import { RequireAuth } from './features/auth/RequireAuth';
import { VideoDetailPage } from './features/player/VideoDetailPage';
import { UploadPage } from './features/upload/UploadPage';
import { VideoListPage } from './features/videos/VideoListPage';

export function AppRoutes() {
  return (
    <Routes>
      <Route path="/" element={<Navigate to="/login" replace />} />
      <Route element={<RedirectIfAuthenticated />}>
        <Route path="/login" element={<LoginPage />} />
        <Route path="/register" element={<RegisterPage />} />
      </Route>
      <Route element={<RequireAuth />}>
        <Route element={<AppLayout />}>
          <Route path="/videos" element={<VideoListPage />} />
          <Route path="/videos/:id" element={<VideoDetailPage />} />
          <Route path="/upload" element={<UploadPage />} />
        </Route>
      </Route>
      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  );
}
