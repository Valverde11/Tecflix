import { useState } from 'react';

export type FieldErrors<Field extends string> = Partial<Record<Field, string>>;

export interface Form<Field extends string> {
  readonly values: Readonly<Record<Field, string>>;
  readonly errors: FieldErrors<Field>;
  readonly change: (field: Field, value: string) => void;
  readonly setError: (field: Field, message: string) => void;
  /** Muestra los errores de todos los campos y dice si el formulario se puede enviar. */
  readonly validate: () => boolean;
}

export function useForm<Field extends string>(
  initialValues: Readonly<Record<Field, string>>,
  findErrors: (values: Readonly<Record<Field, string>>) => FieldErrors<Field>,
): Form<Field> {
  const [values, setValues] = useState(initialValues);
  const [errors, setErrors] = useState<FieldErrors<Field>>({});

  return {
    values,
    errors,
    change: (field, value) => {
      setValues((current) => ({ ...current, [field]: value }));
      // El mensaje ya no describe lo que hay escrito; se vuelve a validar al enviar.
      setErrors((current) => ({ ...current, [field]: undefined }));
    },
    setError: (field, message) => {
      setErrors((current) => ({ ...current, [field]: message }));
    },
    validate: () => {
      const found = findErrors(values);
      setErrors(found);
      return Object.keys(found).length === 0;
    },
  };
}
