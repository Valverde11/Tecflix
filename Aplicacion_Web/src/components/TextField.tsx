import { useId, type ComponentProps } from 'react';
import styles from './TextField.module.css';

interface TextFieldProps extends Omit<ComponentProps<'input'>, 'onChange' | 'value'> {
  readonly label: string;
  readonly value: string;
  readonly error?: string | undefined;
  readonly hint?: string;
  readonly onChange: (value: string) => void;
}

export function TextField({ label, value, error, hint, onChange, ...inputProps }: TextFieldProps) {
  const id = useId();
  const messageId = `${id}-message`;
  const message = error ?? hint;

  return (
    <div className={styles.field}>
      <label htmlFor={id} className={styles.label}>
        {label}
      </label>
      <input
        {...inputProps}
        id={id}
        className={styles.input}
        value={value}
        aria-invalid={error !== undefined}
        aria-describedby={message === undefined ? undefined : messageId}
        onChange={(event) => {
          onChange(event.target.value);
        }}
      />
      {message !== undefined && (
        <p id={messageId} className={error === undefined ? styles.hint : styles.error}>
          {message}
        </p>
      )}
    </div>
  );
}
