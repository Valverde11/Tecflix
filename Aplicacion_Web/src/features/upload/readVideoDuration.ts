const UNKNOWN_DURATION = 0;

/**
 * Lee la duración en el navegador. Solo la web tiene el archivo cuando se crea la metadata;
 * si el navegador no entiende el formato queda como desconocida y la subida sigue adelante.
 */
export function readVideoDuration(file: File): Promise<number> {
  return new Promise((resolve) => {
    const url = URL.createObjectURL(file);
    const video = document.createElement('video');
    const finish = (seconds: number) => {
      URL.revokeObjectURL(url);
      resolve(seconds);
    };
    video.preload = 'metadata';
    video.addEventListener('loadedmetadata', () => {
      finish(Number.isFinite(video.duration) ? Math.round(video.duration) : UNKNOWN_DURATION);
    });
    video.addEventListener('error', () => {
      finish(UNKNOWN_DURATION);
    });
    video.src = url;
  });
}
