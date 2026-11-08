import { describe, expect, it } from 'vitest';
import { JsonObject } from './JsonObject';

describe('JsonObject', () => {
  it.each([[null], ['texto'], [3], [[]]])('rechaza %j porque no es un objeto', (data) => {
    expect(() => new JsonObject(data)).toThrow('se esperaba un objeto');
  });

  it('lee cada tipo de campo', () => {
    const json = new JsonObject({ name: 'Ana', age: 30, tags: ['a', 'b'], kind: 'x' });

    expect(json.string('name')).toBe('Ana');
    expect(json.number('age')).toBe(30);
    expect(json.array('tags')).toEqual(['a', 'b']);
    expect(json.stringArray('tags')).toEqual(['a', 'b']);
    expect(json.oneOf('kind', ['x', 'y'])).toBe('x');
  });

  it('nombra el campo que no cumple', () => {
    const json = new JsonObject({ age: 'treinta', tags: [1], kind: 'z' });

    expect(() => json.string('name')).toThrow('"name" debe ser texto');
    expect(() => json.number('age')).toThrow('"age" debe ser un número');
    expect(() => json.array('age')).toThrow('"age" debe ser una lista');
    expect(() => json.stringArray('tags')).toThrow('"tags" debe ser una lista de textos');
    expect(() => json.oneOf('kind', ['x'])).toThrow('"kind" no admite el valor "z"');
  });
});
