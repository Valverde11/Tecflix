import { readFileSync } from 'node:fs';
import { basename } from 'node:path';
import { expect, test, type Page } from '@playwright/test';

const PASSWORD = 'clave-segura-e2e';
const VIDEO_TITLE = 'Clase de arboles B';
// Con las APIs reales hace falta un video de verdad para transcodificar (E2E_VIDEO_FILE).
const VIDEO_FILE = process.env.E2E_VIDEO_FILE;
const USES_REAL_VIDEO_APIS = process.env.E2E_MOCK_VIDEO_APIS === 'false';
// Transcodificar un video real puede tardar; con los simuladores la respuesta es inmediata.
const PLAYBACK_TIMEOUT_MS = USES_REAL_VIDEO_APIS ? 120_000 : 10_000;
const HAVE_CURRENT_DATA = 2;
// Registrarse calcula dos hashes PBKDF2 (crear y entrar): ~1 s cada uno en Release, hasta
// ~12 s en Debug con la máquina ocupada.
const AUTH_TIMEOUT_MS = 60_000;

/** Nombre único por corrida: la API de usuarios no permite repetirlo. */
function uniqueUsername(): string {
  return `e2e_${Date.now().toString(36)}`;
}

function videoUpload() {
  if (VIDEO_FILE !== undefined) {
    return { name: basename(VIDEO_FILE), mimeType: 'video/mp4', buffer: readFileSync(VIDEO_FILE) };
  }
  // Con los simuladores no se procesa el contenido: basta con bytes que la web pueda enviar.
  return {
    name: `${VIDEO_TITLE}.mp4`,
    mimeType: 'video/mp4',
    buffer: Buffer.from('video de prueba'),
  };
}

async function register(page: Page, username: string) {
  await page.goto('/register');
  await page.getByLabel('Nombre de usuario').fill(username);
  await page.getByLabel('Correo').fill(`${username}@tec.cr`);
  await page.getByLabel('Contraseña', { exact: true }).fill(PASSWORD);
  await page.getByLabel('Repite la contraseña').fill(PASSWORD);
  await page.getByRole('button', { name: 'Crear cuenta' }).click();
  await expect(page.getByRole('heading', { name: 'Videos' })).toBeVisible({
    timeout: AUTH_TIMEOUT_MS,
  });
}

async function logIn(page: Page, username: string) {
  await page.getByLabel('Nombre de usuario').fill(username);
  await page.getByLabel('Contraseña').fill(PASSWORD);
  await page.getByRole('button', { name: 'Iniciar sesión' }).click();
  await expect(page.getByRole('heading', { name: 'Videos' })).toBeVisible({
    timeout: AUTH_TIMEOUT_MS,
  });
}

test('registro → login → subir → reproducir', async ({ page }) => {
  const username = uniqueUsername();

  await test.step('registro', async () => {
    await register(page, username);
    await page.getByRole('button', { name: 'Cerrar sesión' }).click();
    await expect(page).toHaveURL(/\/login$/);
  });

  await test.step('login', async () => {
    await logIn(page, username);
    await expect(page.getByText(username)).toBeVisible();
  });

  await test.step('subir', async () => {
    await page.getByRole('link', { name: 'Subir video' }).click();
    await expect(page.getByRole('meter')).toBeVisible();
    await page.getByLabel('Archivo de video').setInputFiles(videoUpload());
    await page.getByLabel('Título').fill(VIDEO_TITLE);
    await page.getByRole('button', { name: 'Subir video' }).click();
    await expect(page.getByRole('heading', { name: VIDEO_TITLE })).toBeVisible();
  });

  await test.step('reproducir', async () => {
    const player = page.getByLabel(`Reproductor: ${VIDEO_TITLE}`);
    await expect(player).toBeVisible({ timeout: PLAYBACK_TIMEOUT_MS });
    // Los simuladores no sirven HLS: solo con las APIs reales se puede exigir imagen.
    if (USES_REAL_VIDEO_APIS) {
      await expect(page.getByRole('alert')).toHaveCount(0);
      await expect
        .poll(() => player.evaluate((video: HTMLVideoElement) => video.readyState), {
          timeout: PLAYBACK_TIMEOUT_MS,
        })
        .toBeGreaterThanOrEqual(HAVE_CURRENT_DATA);
    }
  });
});
