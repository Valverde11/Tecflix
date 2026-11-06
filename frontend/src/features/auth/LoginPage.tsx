import { Link } from 'react-router';
import { TextField } from '../../components/TextField';
import styles from './AuthForm.module.css';
import { AuthLayout } from './AuthLayout';
import { useLoginForm } from './useLoginForm';

export function LoginPage() {
  const { form, isSubmitting, formError, submit } = useLoginForm();

  return (
    <AuthLayout title="Iniciar sesión">
      <form className={styles.form} onSubmit={submit} noValidate>
        <TextField
          label="Nombre de usuario"
          autoComplete="username"
          value={form.values.username}
          error={form.errors.username}
          onChange={(value) => {
            form.change('username', value);
          }}
        />
        <TextField
          label="Contraseña"
          type="password"
          autoComplete="current-password"
          value={form.values.password}
          error={form.errors.password}
          onChange={(value) => {
            form.change('password', value);
          }}
        />
        {formError !== null && (
          <p role="alert" className={styles.formError}>
            {formError}
          </p>
        )}
        <button type="submit" className={styles.submit} disabled={isSubmitting}>
          {isSubmitting ? 'Iniciando sesión…' : 'Iniciar sesión'}
        </button>
      </form>
      <p className={styles.switch}>
        ¿No tienes cuenta? <Link to="/register">Crear cuenta</Link>
      </p>
    </AuthLayout>
  );
}
