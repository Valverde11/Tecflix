import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { readVideoDuration } from './readVideoDuration';

const OBJECT_URL = 'blob:video';

const revokeObjectURL = vi.fn<(url: string) => void>();
const file = new File(['x'], 'clase.mp4');

function fireOnVideo(type: string, duration?: number) {
  const element = document.createElement('video');
  if (duration !== undefined) {
    Object.defineProperty(element, 'duration', { value: duration });
  }
  vi.spyOn(document, 'createElement').mockImplementation(() => {
    queueMicrotask(() => element.dispatchEvent(new Event(type)));
    return element;
  });
}

beforeEach(() => {
  vi.stubGlobal('URL', Object.assign(URL, { createObjectURL: () => OBJECT_URL, revokeObjectURL }));
});

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  revokeObjectURL.mockReset();
});

describe('readVideoDuration', () => {
  it('redondea la duración y libera la URL temporal', async () => {
    fireOnVideo('loadedmetadata', 90.6);

    await expect(readVideoDuration(file)).resolves.toBe(91);
    expect(revokeObjectURL).toHaveBeenCalledWith(OBJECT_URL);
  });

  it('da 0 si la duración no es un número finito', async () => {
    fireOnVideo('loadedmetadata', Infinity);

    await expect(readVideoDuration(file)).resolves.toBe(0);
  });

  it('da 0 si el navegador no entiende el archivo', async () => {
    fireOnVideo('error');

    await expect(readVideoDuration(file)).resolves.toBe(0);
    expect(revokeObjectURL).toHaveBeenCalledWith(OBJECT_URL);
  });
});
