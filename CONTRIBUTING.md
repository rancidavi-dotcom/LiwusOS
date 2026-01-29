# Contributing to LiwusOS

Thank you for your interest in contributing to LiwusOS! This document outlines the guidelines for contributing to the project.

## Code Style

### C Code

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters max
- **Braces**: K&R style (opening brace on same line)
- **Naming**: 
  - Functions: \snake_case\
  - Types/structs: \PascalCase\ or \snake_case_t\
  - Macros/constants: \UPPER_SNAKE_CASE\
  - Variables: \snake_case\
- **Headers**: Include guards (\#pragma once\ preferred) or traditional \#ifndef\
- **Comments**: Document public APIs, complex algorithms, and hardware-specific code

### Assembly

- **Syntax**: NASM syntax (Intel syntax)
- **Indentation**: 4 spaces
- **Labels**: \snake_case\ with module prefix
- **Comments**: Explain register usage and hardware interactions

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

\\\
<type>(<scope>): <subject>

<body>

<footer>
\\\

**Types:**
- \eat\: New feature
- \ix\: Bug fix
- \efactor\: Code restructuring
- \perf\: Performance improvement
- \docs\: Documentation only
- \chore\: Build, CI, deps, cleanup
- \	est\: Adding tests
- \evert\: Reverting commits

**Scopes:** \kernel\, \sched\, \mm\, \
et\, \s\, \drivers/<name>\, \gui\, \oot\, \uild\

**Examples:**
\\\
feat(drivers/rtl8139): add multicast filter support

fix(sched): prevent priority inversion in mutex

refactor(mm): simplify page table walk

docs: update README with UEFI build instructions
\\\

## Testing

### Before Submitting

1. **Build test**: \make liwusos.iso\ and \make uefi-iso\ must succeed
2. **QEMU test**: Boot in QEMU (BIOS and UEFI) and verify:
   - Kernel boots to shell
   - Network works (ping, HTTP)
   - GUI launches (if applicable)
3. **Static analysis**: Run \make check\ if available

### QEMU Test Commands

\\\ash
# BIOS mode
qemu-system-x86_64 -cdrom liwusos.iso -m 512 -device qemu-xhci -device usb-kbd \
  -netdev user,id=net0 -device rtl8139,netdev=net0 \
  -display none -serial stdio -monitor none

# UEFI mode (requires OVMF)
qemu-system-x86_64 -cdrom liwusos-uefi.iso \
  -bios /usr/share/OVMF/OVMF_CODE_4M.fd -m 512
\\\

## Pull Request Process

1. **Fork** the repository
2. **Create a branch** from \main\ with descriptive name: \eat/drivers/e1000\, \ix/net/tcp-retransmit\
3. **Make changes** with clear, focused commits
4. **Update documentation** if needed (README, comments, etc.)
5. **Run tests** (build + QEMU)
6. **Open PR** with:
   - Clear title following Conventional Commits
   - Description of changes and motivation
   - Test results (QEMU screenshots/logs if applicable)
   - Any breaking changes noted

## Code Review

- All PRs require at least one review
- Address all review comments
- CI must pass (build + QEMU test)
- Squash commits if requested by maintainer

## Reporting Bugs

Use the GitHub issue template with:
- LiwusOS version/commit hash
- QEMU version and command line
- Host OS and hardware (if real hardware)
- Steps to reproduce
- Expected vs actual behavior
- Serial log / screenshot

## Feature Requests

Open an issue with:
- Clear description of the feature
- Use case / motivation
- Proposed implementation approach (if known)
- Any hardware requirements

## License

By contributing, you agree that your contributions will be licensed under the MIT License (for kernel code) and appropriate licenses for third-party code.
