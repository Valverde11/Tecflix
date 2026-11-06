import { Link } from 'react-router';
import { TextField } from '../../components/TextField';
import styles from './AuthForm.module.css';
import { AuthLayout } from './AuthLayout';
import { PASSWORD_RULE, USERNAME_RULE } from './registerValidation';
import { useRegisterForm } from './useRegisterForm';

export function RegisterPage() {
  const { form, isSubmitting, formError, submit } = useRegisterForm();

  return (
    <AuthLayout title="Crear cuenta">
      <form className={styles.form} onSubmit={submit} noValidate>
        <TextField
          label="Nombre de usuario"
          autoComplete="username"
          hint={USERNAME_RULE}
          value={form.values.username}
          error={form.errors.username}
          onChange={(value) => {
            form.change('username', value);
          }}
        />
        <TextField
          label="Correo"
          type="email"
          autoComplete="email"
          value={form.values.email}
          error={form.errors.email}
          onChange={(value) => {
            form.change('email', value);
          }}
        />
        <TextField
          label="Contraseña"
          type="password"
          autoComplete="new-password"
          hint={PASSWORD_RULE}
          value={form.values.password}
          error={form.errors.password}
          onChange={(value) => {
            form.change('password', value);
          }}
        />
        <TextField
          label="Repite la contraseña"
          type="password"
          autoComplete="new-password"
          value={form.values.passwordConfirmation}
          error={form.errors.passwordConfirmation}
          onChange={(value) => {
            form.change('passwordConfirmation', value);
          }}
        />
        {formError !== null && (
          <p role="alert" className={styles.formError}>
            {formError}
          </p>
        )}
        <button type="submit" className={styles.submit} disabled={isSubmitting}>
          {isSubmitting ? 'Creando cuenta…' : 'Crear cuenta'}
        </button>
      </form>
      <p className={styles.switch}>
        ¿Ya tienes cuenta? <Link to="/login">Iniciar sesión</Link>
      </p>
    </AuthLayout>
  );
}
