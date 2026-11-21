# NOVA Terminal roadmap

## 0.2.0 — interactive foundation

- [x] C project layout and Termux Makefile
- [x] Colored prompt and categorized status/error output
- [x] Built-ins: `help`, `version`, `cd`, `clear`, `exit`
- [x] Launch external programs with `fork`/`execvp`
- [x] Interactive history with Up/Down and persistent `~/.nova_history`
- [x] Cursor movement, Backspace, Ctrl-C and Ctrl-D handling
- [x] Tab completion for filesystem paths (command-name completion remains planned)
- [x] Simple line-oriented `edit` utility
- [x] `workspace init` and `workspace info`
- [x] `kernel status` and `kernel init` (project scaffold only; not device-kernel operations)
- [ ] Automated tests and Termux validation

## Later

- Better terminal capability detection and configurable themes.
- Safer command parsing and selected shell-like conveniences.
- Plugin/extension interface only after the core stabilizes.

## Scope rules

NOVA must stay lightweight. Do not claim POSIX-shell compatibility for features that have not been implemented and tested. Avoid heavyweight dependencies when a small C implementation is sufficient.
