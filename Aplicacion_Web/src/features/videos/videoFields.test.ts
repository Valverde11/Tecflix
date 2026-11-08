import { describe, expect, it } from 'vitest';
import { testVideo } from '../../test/fixtures';
import {
  EMPTY_VIDEO_FIELDS,
  findVideoErrors,
  parseTags,
  toFieldValues,
  toVideoDetails,
} from './videoFields';

const valid = { ...EMPTY_VIDEO_FIELDS, title: 'Árboles B' };

describe('parseTags', () => {
  it.each([
    ['', []],
    ['a', ['a']],
    [' a , b ,, ', ['a', 'b']],
    ['a, b, a', ['a', 'b']],
  ])('convierte "%s"', (text, tags) => {
    expect(parseTags(text)).toEqual(tags);
  });
});

describe('findVideoErrors', () => {
  it('solo exige el título', () => {
    expect(findVideoErrors(valid)).toEqual({});
  });

  it.each(['', '   '])('rechaza el título vacío "%s"', (title) => {
    expect(findVideoErrors({ ...valid, title }).title).toBe('Escribe un título.');
  });

  it('mide en bytes, como metadata', () => {
    expect(findVideoErrors({ ...valid, title: 'a'.repeat(200) }).title).toBeUndefined();
    expect(findVideoErrors({ ...valid, title: 'ñ'.repeat(101) }).title).toContain('200');
  });

  it('limita tema y descripción', () => {
    const errors = findVideoErrors({
      ...valid,
      topic: 'a'.repeat(101),
      description: 'a'.repeat(5001),
    });

    expect(errors.topic).toContain('100');
    expect(errors.description).toContain('5000');
  });

  it('limita la cantidad y el largo de las etiquetas', () => {
    const many = Array.from({ length: 21 }, (_item, index) => `t${String(index)}`).join(',');

    expect(findVideoErrors({ ...valid, tags: many }).tags).toContain('20');
    expect(findVideoErrors({ ...valid, tags: 'a'.repeat(101) }).tags).toContain('100');
  });
});

describe('conversiones', () => {
  it('prepara los datos de un video para editarlos', () => {
    const values = toFieldValues({ ...testVideo, tags: ['a', 'b'] });

    expect(values).toMatchObject({ title: testVideo.title, tags: 'a, b' });
  });

  it('limpia los campos al enviarlos', () => {
    const fields = { title: ' T ', topic: ' Redes ', description: ' D ', tags: 'a, b' };

    expect(toVideoDetails(fields, 'public')).toEqual({
      title: 'T',
      topic: 'Redes',
      description: 'D',
      tags: ['a', 'b'],
      visibility: 'public',
    });
  });
});
