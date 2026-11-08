import { describe, expect, it } from 'vitest';
import { formatDuration, formatUploadDate, topicColor } from './videoFormat';

describe('formatDuration', () => {
  it.each([
    [1, '0:01'],
    [90, '1:30'],
    [599, '9:59'],
    [3600, '1:00:00'],
    [3725, '1:02:05'],
    [90.7, '1:30'],
  ])('%d segundos se muestran como %s', (seconds, text) => {
    expect(formatDuration(seconds)).toBe(text);
  });

  it('marca como desconocida la duración de un video sin procesar', () => {
    expect(formatDuration(0)).toBe('--:--');
  });
});

describe('formatUploadDate', () => {
  it('muestra el año y el día', () => {
    const text = formatUploadDate('2026-11-03T15:04:05Z');

    expect(text).toContain('2026');
    expect(text).toContain('3');
  });
});

describe('topicColor', () => {
  it('siempre da el mismo color al mismo tema', () => {
    expect(topicColor('Redes')).toBe(topicColor('Redes'));
  });

  it('usa una variable de la paleta aunque el tema esté vacío o sea muy largo', () => {
    expect(topicColor('')).toMatch(/^var\(--bar-[a-z]+\)$/);
    expect(topicColor('tema '.repeat(200))).toMatch(/^var\(--bar-[a-z]+\)$/);
  });

  it('un tema muy largo no cae siempre en el mismo color', () => {
    const colors = ['a', 'b', 'c', 'd', 'e', 'f'].map((end) =>
      topicColor('tema '.repeat(500) + end),
    );

    expect(new Set(colors).size).toBeGreaterThan(1);
  });

  it('distribuye temas distintos entre varios colores', () => {
    const topics = ['Redes', 'Video', 'Programación', 'Estructuras de datos', 'Física', 'Cálculo'];

    expect(new Set(topics.map(topicColor)).size).toBeGreaterThanOrEqual(4);
  });

  it('depende del orden de las letras, no solo de cuáles son', () => {
    const anagrams = ['Redes', 'Sedre', 'Deres', 'Seder', 'Eersd', 'Dseer'];

    expect(new Set(anagrams.map(topicColor)).size).toBeGreaterThan(1);
  });
});
