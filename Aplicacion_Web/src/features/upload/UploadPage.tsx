import { VideoFieldsForm } from '../videos/VideoFieldsForm';
import { formatBytes } from './formatBytes';
import { QuotaMeter } from './QuotaMeter';
import styles from './UploadPage.module.css';
import { useUploadForm } from './useUploadForm';

const PERCENT = 100;

function UploadProgress({ progress, onCancel }: { progress: number; onCancel: () => void }) {
  const percent = Math.round(progress * PERCENT);
  return (
    <div className={styles.progress}>
      <p role="status">
        {percent < PERCENT ? `Subiendo: ${String(percent)} %` : 'Guardando los datos del video…'}
      </p>
      <progress aria-label="Avance de la subida" max={PERCENT} value={percent} />
      <button type="button" className={styles.secondary} onClick={onCancel}>
        Cancelar subida
      </button>
    </div>
  );
}

export function UploadPage() {
  const upload = useUploadForm();
  const { form, file, fileError, visibility, formError } = upload;

  return (
    <div className={styles.page}>
      <h1>Subir video</h1>
      <QuotaMeter />
      {upload.pendingRecovery !== null && (
        <section role="status" className={styles.progress}>
          <p>
            Hay una subida pendiente de registrar en metadata: <code>{upload.pendingRecovery.input.videoId}</code>.
            No vuelvas a subir ese archivo. Esta operación pertenece a tu cuenta.
          </p>
          {upload.recoveryError !== null && <p className={styles.formError}>{upload.recoveryError}</p>}
          <button type="button" className={styles.secondary}
            disabled={upload.isRecovering} onClick={upload.retryRecovery}>
            {upload.isRecovering ? 'Recuperando metadata…' : 'Reintentar registro pendiente'}
          </button>
        </section>
      )}
      {upload.isQuotaKnown && !upload.canUpload && (
        <p role="alert" className={styles.formError}>
          Llegaste al límite de tu espacio. Borra un video para subir otro.
        </p>
      )}
      <form className={styles.form} onSubmit={upload.submit} noValidate>
        <fieldset className={styles.fieldset} disabled={upload.isUploading}>
          <div className={styles.field}>
            <label htmlFor="video-file" className={styles.label}>
              Archivo de video
            </label>
            <input
              id="video-file"
              type="file"
              accept="video/*"
              aria-invalid={fileError !== null}
              aria-describedby={fileError === null ? undefined : 'video-file-error'}
              onChange={(event) => {
                upload.changeFile(event.target.files?.[0] ?? null);
              }}
            />
            {file !== null && <p className={styles.hint}>{formatBytes(file.size)}</p>}
            {fileError !== null && (
              <p id="video-file-error" className={styles.fileError}>
                {fileError}
              </p>
            )}
          </div>
          <VideoFieldsForm form={form} />
          <fieldset className={styles.visibility}>
            <legend className={styles.label}>¿Quién puede verlo?</legend>
            <label>
              <input
                type="radio"
                name="visibility"
                checked={visibility === 'private'}
                onChange={() => {
                  upload.changeVisibility('private');
                }}
              />{' '}
              Solo yo
            </label>
            <label>
              <input
                type="radio"
                name="visibility"
                checked={visibility === 'public'}
                onChange={() => {
                  upload.changeVisibility('public');
                }}
              />{' '}
              Todos
            </label>
          </fieldset>
        </fieldset>
        {formError !== null && (
          <p role="alert" className={styles.formError}>
            {formError}
          </p>
        )}
        {upload.isUploading ? (
          <UploadProgress progress={upload.progress} onCancel={upload.cancel} />
        ) : (
          <button type="submit" className={styles.submit} disabled={!upload.canUpload}>
            Subir video
          </button>
        )}
      </form>
    </div>
  );
}
