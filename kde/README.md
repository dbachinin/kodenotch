# Kodenotch for KDE Plasma

This directory contains ports for Plasma 5.27 and Plasma 6. It reads
Claude Code, Codex, and Antigravity rate limits and presents them as a panel widget.

## Build and install

Install the Qt and KDE Frameworks development files matching your Plasma
version, Extra CMake Modules, CMake and a C++20 compiler.

For Plasma 6:

```sh
cmake -S kde -B build/kde -DCMAKE_BUILD_TYPE=Release \
  -DPLASMA_MAJOR_VERSION=6 \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/kde
ctest --test-dir build/kde --output-on-failure
sudo cmake --install build/kde
```

For Plasma 5.27, use a separate build directory:

```sh
cmake -S kde -B build/kde5 -DCMAKE_BUILD_TYPE=Release \
  -DPLASMA_MAJOR_VERSION=5 \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/kde5
ctest --test-dir build/kde5 --output-on-failure
sudo cmake --install build/kde5
```

The native QML module must be installed in Qt's system import directory. For
packaging, use `DESTDIR` instead of installing directly and let the distribution
package own the files under `/usr`.

Backend tests can be built without Plasma development files by adding
`-DBUILD_PLASMOID=OFF`; `PLASMA_MAJOR_VERSION` then selects Qt 5 or Qt 6.

Ubuntu 24.04 does not ship KF6 development packages. Build against an isolated
KDE Neon SDK without changing the host repositories:

```sh
docker build -t codenotch-kde-sdk kde
docker run --rm --user 1000:1000 -v "$PWD:/src" -w /src \
  codenotch-kde-sdk cmake -S kde -B build/kde-neon -G Ninja \
  -DPLASMA_MAJOR_VERSION=6
docker run --rm --user 1000:1000 -v "$PWD:/src" -w /src \
  codenotch-kde-sdk cmake --build build/kde-neon
docker run --rm --user 1000:1000 -v "$PWD:/src" -w /src \
  codenotch-kde-sdk ctest --test-dir build/kde-neon --output-on-failure
```

Restart Plasma or log in again, then add **Kodenotch** to a panel. A compiled
QML plugin cannot be distributed through the KDE Store as a QML-only package;
distribution should use native packages for each Linux distribution.

## Security properties

- All backends start disabled and begin work only after Plasma applies the
  widget settings.
- Disabling an integration cancels its process or network request, invalidates
  in-flight work and erases the displayed reading.
- Only an executable owned by the current user or root and not writable by
  group/others is launched.
- Provider output is capped at 1 MiB, parsed as JSON and never written to logs.
- Percentages and reset timestamps are validated before they reach QML.
- Claude credentials are accepted only from a regular, user-owned private file.
  The already-open file descriptor is checked to prevent path replacement;
  requests use a fixed HTTPS endpoint and refuse redirects.
- Antigravity usage is queried from the local language server on loopback
  using the CSRF token extracted from the process table, with fallback to
  local transcript activity counting.

Cursor and GLM are deliberately deferred until their actual Linux credential
stores and process layouts can be verified. Guessing those locations would
risk reading or sending the wrong credential.
