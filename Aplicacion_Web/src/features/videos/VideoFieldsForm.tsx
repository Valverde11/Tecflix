import { useId } from 'react';
import { TextField } from '../../components/TextField';
import textFieldStyles from '../../components/TextField.module.css';
import type { Form } from '../../hooks/useForm';
import type { VideoField } from './videoFields';

interface VideoFieldsFormProps {
  readonly form: Form<VideoField>;
}

/** Campos comunes a subir y editar: las dos pantallas validan y envían lo mismo. */
export function VideoFieldsForm({ form }: VideoFieldsFormProps) {
  const descriptionId = useId();
  const descriptionMessageId = `${descriptionId}-message`;
  const descriptionError = form.errors.description;

  return (
    <>
      <TextField
        label="Título"
        value={form.values.title}
        error={form.errors.title}
        onChange={(value) => {
          form.change('title', value);
        }}
      />
      <TextField
        label="Tema"
        value={form.values.topic}
        error={form.errors.topic}
        onChange={(value) => {
          form.change('topic', value);
        }}
      />
      <div className={textFieldStyles.field}>
        <label htmlFor={descriptionId} className={textFieldStyles.label}>
          Descripción
        </label>
        <textarea
          id={descriptionId}
          className={textFieldStyles.input}
          rows={4}
          value={form.values.description}
          aria-invalid={descriptionError !== undefined}
          aria-describedby={descriptionError === undefined ? undefined : descriptionMessageId}
          onChange={(event) => {
            form.change('description', event.target.value);
          }}
        />
        {descriptionError !== undefined && (
          <p id={descriptionMessageId} className={textFieldStyles.error}>
            {descriptionError}
          </p>
        )}
      </div>
      <TextField
        label="Etiquetas"
        hint="Sepáralas con comas."
        value={form.values.tags}
        error={form.errors.tags}
        onChange={(value) => {
          form.change('tags', value);
        }}
      />
    </>
  );
}
