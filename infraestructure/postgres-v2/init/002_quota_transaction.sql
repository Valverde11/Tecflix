
CREATE OR REPLACE FUNCTION apply_quota_operation(
    p_user_id BIGINT,
    p_operation_id TEXT,
    p_delta_bytes BIGINT
)
RETURNS TABLE (
    quota_bytes BIGINT,
    used_bytes BIGINT,
    already_applied BOOLEAN
)
LANGUAGE plpgsql
AS $$
DECLARE
    v_user users%ROWTYPE;
    v_previous quota_operations%ROWTYPE;
    v_new_usage NUMERIC;
BEGIN
    IF p_operation_id IS NULL
       OR length(p_operation_id) NOT BETWEEN 1 AND 128
       OR p_operation_id !~ '^[A-Za-z0-9_-]+$'
    THEN
        RAISE EXCEPTION 'operationId inválido';
    END IF;

    IF p_delta_bytes IS NULL OR p_delta_bytes = 0 THEN
        RAISE EXCEPTION 'deltaBytes no puede ser cero';
    END IF;

    -- Serializar operaciones que afectan al mismo usuario.
    SELECT u.*
      INTO v_user
      FROM users AS u
     WHERE u.id = p_user_id
       AND u.deleted = FALSE
     FOR UPDATE;

    IF NOT FOUND THEN
        RAISE EXCEPTION 'El usuario no existe';
    END IF;

    -- Verificar si la operación ya se aplicó.
    SELECT q.*
      INTO v_previous
      FROM quota_operations AS q
     WHERE q.operation_id = p_operation_id;

    IF FOUND THEN
        IF v_previous.user_id <> p_user_id
           OR v_previous.delta_bytes <> p_delta_bytes
        THEN
            RAISE EXCEPTION
                'operationId reutilizado con otros datos';
        END IF;

        RETURN QUERY
        SELECT
            v_previous.result_quota_bytes,
            v_previous.after_bytes,
            TRUE;

        RETURN;
    END IF;

    -- NUMERIC evita desbordamiento antes de validar.
    v_new_usage :=
        v_user.used_bytes::NUMERIC + p_delta_bytes::NUMERIC;

    IF v_new_usage < 0 THEN
        RAISE EXCEPTION 'Liberación superior al consumo';
    END IF;

    IF v_new_usage > v_user.quota_bytes THEN
        RAISE EXCEPTION 'Cuota excedida';
    END IF;

    -- Registrar el resultado y actualizar el usuario
    -- dentro de la misma transacción.
    INSERT INTO quota_operations (
        operation_id,
        user_id,
        delta_bytes,
        before_bytes,
        after_bytes,
        result_quota_bytes
    )
    VALUES (
        p_operation_id,
        p_user_id,
        p_delta_bytes,
        v_user.used_bytes,
        v_new_usage::BIGINT,
        v_user.quota_bytes
    );

    UPDATE users AS u
       SET used_bytes = v_new_usage::BIGINT
     WHERE u.id = p_user_id;

    RETURN QUERY
    SELECT
        v_user.quota_bytes,
        v_new_usage::BIGINT,
        FALSE;
END;
$$;

