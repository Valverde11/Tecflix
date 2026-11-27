
CREATE TABLE users (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,

    username VARCHAR(32) NOT NULL,
    username_key VARCHAR(32) NOT NULL,

    email VARCHAR(64) NOT NULL,
    email_key VARCHAR(64) NOT NULL,

    password_hash BYTEA NOT NULL,
    salt BYTEA NOT NULL,

    quota_bytes BIGINT NOT NULL DEFAULT 1000000000,
    used_bytes BIGINT NOT NULL DEFAULT 0,

    created_at BIGINT NOT NULL,
    deleted BOOLEAN NOT NULL DEFAULT FALSE,

    CONSTRAINT valid_id CHECK (id > 0),
    CONSTRAINT valid_password_hash
        CHECK (octet_length(password_hash) = 32),
    CONSTRAINT valid_salt
        CHECK (octet_length(salt) = 16),
    CONSTRAINT valid_quota
        CHECK (
            quota_bytes >= 0
            AND used_bytes >= 0
            AND used_bytes <= quota_bytes
        )
);

CREATE UNIQUE INDEX users_username_unique
    ON users (username_key)
    WHERE deleted = FALSE;

CREATE UNIQUE INDEX users_email_unique
    ON users (email_key)
    WHERE deleted = FALSE;

CREATE TABLE quota_operations (
    operation_id VARCHAR(128) PRIMARY KEY,

    user_id BIGINT NOT NULL,
    delta_bytes BIGINT NOT NULL,

    before_bytes BIGINT NOT NULL,
    after_bytes BIGINT NOT NULL,

    result_quota_bytes BIGINT NOT NULL,
    committed_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT valid_quota_delta
        CHECK (delta_bytes <> 0),

    CONSTRAINT valid_quota_result
        CHECK (
            before_bytes >= 0
            AND after_bytes >= 0
            AND result_quota_bytes >= 0
            AND after_bytes <= result_quota_bytes
        )
);

CREATE INDEX quota_operations_user_idx
    ON quota_operations (user_id);

