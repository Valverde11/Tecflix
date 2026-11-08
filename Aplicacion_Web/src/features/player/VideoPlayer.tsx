import { useEffect, useRef, useState } from 'react';
import { PlaybackSession } from './PlaybackSession';
import styles from './VideoPlayer.module.css';

interface VideoPlayerProps {
  readonly manifestUrl: string;
  readonly token: string | null;
  readonly title: string;
}

export function VideoPlayer({ manifestUrl, token, title }: VideoPlayerProps) {
  const videoRef = useRef<HTMLVideoElement>(null);
  const [failure, setFailure] = useState<string | null>(null);
  const [attempt, setAttempt] = useState(0);
  const previousRelease = useRef(Promise.resolve());

  useEffect(() => {
    if (videoRef.current === null) {
      return undefined;
    }
    const session = new PlaybackSession(videoRef.current, setFailure);
    // Dos sesiones comparten el <video>: si la nueva empieza mientras la anterior se destruye,
    // esa destrucción le quita la fuente y la pantalla se queda sin imagen.
    const canStart = previousRelease.current;
    void canStart.then(() => session.start(manifestUrl, token));
    return () => {
      // Aunque no se pueda liberar, la siguiente sesión debe poder intentarlo.
      previousRelease.current = canStart.then(() => session.stop()).catch(() => undefined);
    };
  }, [manifestUrl, token, attempt]);

  return (
    <div className={styles.screen}>
      <video
        ref={videoRef}
        className={styles.video}
        controls
        aria-label={`Reproductor: ${title}`}
      />
      {failure !== null && (
        <div role="alert" className={styles.failure}>
          <p>{failure}</p>
          <button
            type="button"
            className={styles.retry}
            onClick={() => {
              setFailure(null);
              setAttempt((current) => current + 1);
            }}
          >
            Intentar de nuevo
          </button>
        </div>
      )}
    </div>
  );
}
