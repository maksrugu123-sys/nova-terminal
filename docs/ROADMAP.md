# NOVA Terminal roadmap

## 0.2.0 — interactive foundation

- [x] C project layout and Termux Makefile
- [x] Colored prompt and categorized status/error output
- [x] Five selectable ANSI color themes
- [x] File/directory color distinction in completion results
- [x] UTF-8 input support for Android keyboard text
- [x] Built-ins: `help`, `version`, `cd`, `clear`, `exit`
- [x] Launch external programs with `fork`/`execvp`
- [x] Interactive history with Up/Down and persistent `~/.nova_history`
- [x] Cursor movement, Backspace, Ctrl-C and Ctrl-D handling
- [x] Tab completion for filesystem paths (command-name completion remains planned)
- [x] Simple line-oriented `edit` utility
- [x] `workspace init` and `workspace info`
- [x] `kernel status` and `kernel init` (project scaffold only; not device-kernel operations)
- [x] Basic background process tracking (`&`, `jobs`, `wait`, `kill`).
- [x] Foreground Ctrl+C interruption and Ctrl+Z stop/resume (`fg`).
- [x] Lightweight shell smoke tests in CI.
- [x] Initial shell lexer module: quoted words, variable expansion and operator tokens (`|`, `<`, `>`, `>>`, `&&`, `||`, `&`)
- [x] Dedicated lexer unit tests included in `make test`
- [ ] Integrate lexer into command execution; add syntax validation and parser tests
- [ ] Implement pipelines, redirection and conditional execution
- [ ] Automated tests beyond smoke coverage and Termux device validation

## Later

- Better terminal capability detection and configurable themes.
- Full terminal process-group management and foreground/background terminal handoff.
- Safer command parsing, quoting, pipelines and redirection.
- Plugin/extension interface only after the core stabilizes.

## Scope rules

NOVA must stay lightweight. Do not claim POSIX-shell compatibility for features that have not been implemented and tested. Avoid heavyweight dependencies when a small C implementation is sufficient.
