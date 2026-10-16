# Cómo publicar Ikemen NX en GitHub

Estos pasos suben el código (este repositorio) y publican el paquete de la SD como *Release*.

## 1. Antes de empezar

- Crea una cuenta en <https://github.com> si no tienes.
- Instala **GitHub CLI**: <https://cli.github.com/>. En Windows, desde PowerShell:

  ```powershell
  winget install GitHub.cli
  ```

- Inicia sesión una vez:

  ```bash
  gh auth login
  ```

  Elige *GitHub.com*, *HTTPS* y *Login with a web browser*.

- Decide con qué nombre y correo firmarás los commits. Para no publicar tu correo personal o de trabajo, usa el correo privado que te da GitHub. Lo encuentras en <https://github.com/settings/emails>, con el formato `12345678+usuario@users.noreply.github.com`. Configúralo en este repositorio:

  ```bash
  cd ikemen-nx   # la carpeta de este repositorio
  git config user.name "TU_NOMBRE_O_USUARIO"
  git config user.email "12345678+usuario@users.noreply.github.com"
  ```

## 2. Revisar el commit inicial

El repositorio ya trae un commit inicial firmado como "Ikemen NX". Si quieres que salga a tu nombre, después del paso 1 ejecuta:

```bash
git commit --amend --reset-author --no-edit
```

## 3. Crear el repositorio en GitHub y subir el código

```bash
gh repo create ikemen-nx --public --source . --remote origin --push \
  --description "Ikemen GO (MUGEN-compatible fighting engine) for Nintendo Switch homebrew"
```

Eso crea `https://github.com/TU_USUARIO/ikemen-nx` y sube la rama `main`.

Si prefieres la web: crea un repositorio vacío llamado `ikemen-nx` (sin README ni licencia) y luego:

```bash
git remote add origin https://github.com/TU_USUARIO/ikemen-nx.git
git push -u origin main
```

## 4. Publicar la versión (Release) con el paquete de la SD

Con el paquete ya generado por `./package.sh`:

```bash
git tag v0.3.0
git push origin v0.3.0
gh release create v0.3.0 out/ikemen-nx-v0.3.0-sd.zip out/ikemen.nro out/nxconfig.nro \
  --title "Ikemen NX v0.3.0" --notes-file docs/release-v0.3.0.md
```

Si prefieres la web: en tu repositorio, entra a *Releases → Draft a new release*, elige la etiqueta `v0.3.0`, pega el texto de `docs/release-v0.3.0.md` y arrastra los tres archivos.

## 5. Antes de publicar, revisa

- Que el zip **no contenga contenido de terceros** (juegos completos, personajes o screenpacks que no sean los de Ikemen GO). `package.sh` solo copia los datos oficiales de Ikemen GO v1.0.0.
- Que el README y la Release mencionen las licencias (ver `THIRD_PARTY.md`). Por incluir FFmpeg con `libpostproc` (GPL), el `.nro` debe ir acompañado del código fuente, que es este mismo repositorio.

## 6. Versiones siguientes

1. Cambia la versión en `app/main.c` (`IKEMEN_NX_VERSION_STR`).
2. Si cambiaste Ikemen o Go en tu copia local, regenera los parches:

   ```bash
   git -C ikemen format-patch -o ../patches/ikemen v1.0.0..HEAD
   ```

   Haz lo mismo con `goroot`, partiendo de la etiqueta `go1.27.2`.
3. Ejecuta `./build.sh`, `./package.sh`, haz commit, crea la etiqueta y la release como en el paso 4.
