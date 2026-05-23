# Faza 10 — CI/CD GitHub Actions

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 9 completă  
**Locație:** `.github/workflows/`

---

## Obiective

Pipeline automat la fiecare `git push`:
1. **Static analysis** — Cppcheck + MISRA C:2012 addon
2. **Build STM32** — `west build` pentru `stm32_min_dev@blue`
3. **Build ESP32** — `west build` pentru `esp32_devkitc/esp32/procpu`
4. **Ztest** — rulare teste pe QEMU
5. **Artifacts** — upload `zephyr.bin` + `zephyr.elf` per commit

---

## Structura Workflow

```
.github/
└── workflows/
    ├── build.yml          # Build STM32 + ESP32
    ├── static_analysis.yml # Cppcheck MISRA
    └── test.yml           # Ztest pe QEMU
```

---

## `build.yml` — Build Ambele Targets

```yaml
name: Build Firmware

on: [push, pull_request]

jobs:
  build-stm32:
    runs-on: ubuntu-latest
    container: zephyrprojectrtos/ci:latest

    steps:
      - uses: actions/checkout@v4

      - name: West init & update
        run: |
          west init -l .
          west update

      - name: Build STM32
        run: |
          west build -b stm32_min_dev@blue \
            application/stm32_app \
            --build-dir build-stm32

      - name: Upload STM32 artifacts
        uses: actions/upload-artifact@v4
        with:
          name: stm32-firmware-${{ github.sha }}
          path: |
            build-stm32/zephyr/zephyr.bin
            build-stm32/zephyr/zephyr.elf

  build-esp32:
    runs-on: ubuntu-latest
    container: zephyrprojectrtos/ci:latest

    steps:
      - uses: actions/checkout@v4

      - name: West init & update
        run: |
          west init -l .
          west update
          west blobs fetch hal_espressif

      - name: Build ESP32
        run: |
          west build -b esp32_devkitc/esp32/procpu \
            application/esp32_app \
            --build-dir build-esp32

      - name: Upload ESP32 artifacts
        uses: actions/upload-artifact@v4
        with:
          name: esp32-firmware-${{ github.sha }}
          path: |
            build-esp32/zephyr/zephyr.bin
            build-esp32/zephyr/zephyr.elf
```

---

## `static_analysis.yml` — Cppcheck MISRA

```yaml
name: Static Analysis (MISRA C:2012)

on: [push, pull_request]

jobs:
  cppcheck:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Install Cppcheck
        run: |
          sudo apt-get install -y cppcheck
          pip install cppcheck-misra

      - name: Run Cppcheck MISRA C:2012
        run: |
          cppcheck \
            --enable=all \
            --std=c11 \
            --addon=misra.py \
            --suppress=missingIncludeSystem \
            --error-exitcode=1 \
            application/common/src/ \
            application/stm32_app/src/ \
            application/esp32_app/src/

      - name: Upload analysis report
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: cppcheck-report-${{ github.sha }}
          path: cppcheck-report.xml
```

---

## `test.yml` — Ztest pe QEMU

```yaml
name: Unit Tests (Ztest / QEMU)

on: [push, pull_request]

jobs:
  ztest:
    runs-on: ubuntu-latest
    container: zephyrprojectrtos/ci:latest

    steps:
      - uses: actions/checkout@v4

      - name: West init & update
        run: west init -l . && west update

      - name: Build & run tests
        run: |
          west build -b qemu_cortex_m3 tests/unit --build-dir build-tests
          timeout 60 west build --build-dir build-tests -t run
        env:
          QEMU_FLAGS: "-nographic -serial stdio"
```

---

## Badges pentru README

```markdown
![Build STM32](https://github.com/USER/REPO/actions/workflows/build.yml/badge.svg)
![MISRA Check](https://github.com/USER/REPO/actions/workflows/static_analysis.yml/badge.svg)
![Tests](https://github.com/USER/REPO/actions/workflows/test.yml/badge.svg)
```

---

## Plan de lucru

1. Creare fișiere workflow în `.github/workflows/`
2. Configurare `west.yml` manifest pentru CI (fără SDK local)
3. Test pipeline pe branch de test
4. Configurare branch protection: merge blocat dacă build/tests eșuează

---

## Criterii de Acceptare

- [ ] Pipeline verde pe `main` branch
- [ ] Cppcheck zero violation-uri MISRA obligatorii
- [ ] Artifacts `zephyr.bin` + `zephyr.elf` descărcabile din fiecare build
- [ ] Branch protection activ — PR necesită build verde
