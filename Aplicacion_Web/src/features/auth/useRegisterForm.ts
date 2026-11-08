import { useMutation } from '@tanstack/react-query';
import type { SyntheticEvent } from 'react';
import { ApiError } from '../../api/ApiError';
import { useForm } from '../../hooks/useForm';
import { useServices } from '../../hooks/useServices';
import type { NewUser, Session } from '../../types/user';
import { describeAuthError } from './authErrors';
import { findRegisterErrors, type RegisterField, type RegisterValues } from './registerValidation';

const EMPTY_REGISTER: RegisterValues = {
  username: '',
  email: '',
  password: '',
  passwordConfirmation: '',
};

const FIELD_BY_ERROR_CODE = new Map<string, RegisterField>([
  ['username_taken', 'username'],
  ['invalid_username', 'username'],
  ['email_taken', 'email'],
  ['invalid_email', 'email'],
  ['invalid_password', 'password'],
]);

function fieldOfError(error: unknown): RegisterField | undefined {
  return error instanceof ApiError ? FIELD_BY_ERROR_CODE.get(error.code) : undefined;
}

export function useRegisterForm() {
  const { usersApi, sessionStore } = useServices();
  const form = useForm<RegisterField>(EMPTY_REGISTER, findRegisterErrors);
  const register = useMutation({
    // Registrar no devuelve token: se inicia sesión para no pedir los mismos datos otra vez.
    mutationFn: async (newUser: NewUser): Promise<Session> => {
      await usersApi.register(newUser);
      return usersApi.login({ username: newUser.username, password: newUser.password });
    },
    onSuccess: (session) => {
      sessionStore.setSession(session);
    },
    onError: (error) => {
      const field = fieldOfError(error);
      if (field !== undefined) {
        form.setError(field, describeAuthError(error));
      }
    },
  });

  const showFormError = register.isError && fieldOfError(register.error) === undefined;

  return {
    form,
    isSubmitting: register.isPending,
    formError: showFormError ? describeAuthError(register.error) : null,
    submit: (event: SyntheticEvent) => {
      event.preventDefault();
      if (form.validate()) {
        const { username, email, password } = form.values;
        register.mutate({ username, email, password });
      }
    },
  };
}
