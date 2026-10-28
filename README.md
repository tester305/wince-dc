# wince-dc

A lightweight Dreamcast homebrew C project configured with a custom Makefile and automated GitHub Actions builds using KallistiOS.

## Project Structure

- `main.c` — Main application entry point.
- `Makefile` — Build rules and target configuration.
- `.github/workflows/build.yml` — CI workflow for automated compilation checks on push.

## Building Locally

### Prerequisites
Make sure your KallistiOS development environment and sh-elf toolchain are set up.

### Compilation
To compile the project, run:
```bash
make
```

To clean up build artifacts:
```bash
make clean
```

## CI/CD
The GitHub Actions workflow in `.github/workflows/build.yml` automatically verifies the build on every push (and also if you dont want to build or do fuss, download it from github actions)
