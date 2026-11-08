import { act, renderHook } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { useForm, type FieldErrors } from './useForm';

type Field = 'name' | 'email';

const EMPTY: Readonly<Record<Field, string>> = { name: '', email: '' };

function requireName(values: Readonly<Record<Field, string>>): FieldErrors<Field> {
  return values.name === '' ? { name: 'Falta el nombre' } : {};
}

function renderForm() {
  return renderHook(() => useForm<Field>(EMPTY, requireName));
}

describe('useForm', () => {
  it('empieza con los valores iniciales y sin errores', () => {
    const { result } = renderForm();

    expect(result.current.values).toEqual(EMPTY);
    expect(result.current.errors).toEqual({});
  });

  it('cambia solo el campo indicado', () => {
    const { result } = renderForm();

    act(() => {
      result.current.change('email', 'ana@tec.cr');
    });

    expect(result.current.values).toEqual({ name: '', email: 'ana@tec.cr' });
  });

  it('valida al enviar y dice si se puede enviar', () => {
    const { result } = renderForm();

    let canSubmit = true;
    act(() => {
      canSubmit = result.current.validate();
    });

    expect(canSubmit).toBe(false);
    expect(result.current.errors).toEqual({ name: 'Falta el nombre' });
  });

  it('borra el error de un campo al editarlo, sin tocar los demás', () => {
    const { result } = renderForm();
    act(() => {
      result.current.setError('name', 'Falta el nombre');
      result.current.setError('email', 'Correo ya usado');
    });

    act(() => {
      result.current.change('name', 'Ana');
    });

    expect(result.current.errors.name).toBeUndefined();
    expect(result.current.errors.email).toBe('Correo ya usado');
  });

  it('acepta el formulario cuando ya no hay errores', () => {
    const { result } = renderForm();
    act(() => {
      result.current.change('name', 'Ana');
    });

    let canSubmit = false;
    act(() => {
      canSubmit = result.current.validate();
    });

    expect(canSubmit).toBe(true);
    expect(result.current.errors).toEqual({});
  });
});
