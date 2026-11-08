import { useMutation } from '@tanstack/react-query';
import type { SyntheticEvent } from 'react';
import { useForm, type FieldErrors } from '../../hooks/useForm';
import { useServices } from '../../hooks/useServices';
import type { Credentials } from '../../types/user';
import { describeAuthError } from './authErrors';

type LoginField = keyof Credentials;

const EMPTY_LOGIN: Credentials = { username: '', password: '' };

function findLoginErrors({ username, password }: Credentials): FieldErrors<LoginField> {
  return {
    ...(username === '' && { username: 'Escribe tu nombre de usuario.' }),
    ...(password === '' && { password: 'Escribe tu contraseña.' }),
  };
}

export function useLoginForm() {
  const { usersApi, sessionStore } = useServices();
  const form = useForm<LoginField>(EMPTY_LOGIN, findLoginErrors);
  const login = useMutation({
    mutationFn: (credentials: Credentials) => usersApi.login(credentials),
    onSuccess: (session) => {
      sessionStore.setSession(session);
    },
  });

  return {
    form,
    isSubmitting: login.isPending,
    formError: login.isError ? describeAuthError(login.error) : null,
    submit: (event: SyntheticEvent) => {
      event.preventDefault();
      if (form.validate()) {
        login.mutate(form.values);
      }
    },
  };
}
