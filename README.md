# NOVA Terminal

A lightweight hybrid command environment for Android and Termux, written in C.

## Project goals

- A small native C command environment for Android/Termux.
- A hybrid model: built-in NOVA commands plus launching external programs through the system PATH.
- A readable, color-rich classic terminal interface.
- Low memory use and no heavyweight runtime dependencies.

## Status

- Previous local prototype: `0.1.0` (reported working in Termux).
- Current repository baseline: being established.
- Next development target: `0.2.0`.

The existing local `0.1.0` installation is not replaced by this repository work. New development will be isolated on `develop/0.2.0`.

## Build on Termux

Install the C toolchain:

```sh
pkg update
pkg install clang make
```

Then, from the repository root:

```sh
make
./nova
```

Install into the Termux prefix:

```sh
make install
```

## Planned 0.2.0 work

- ANSI 256-color interface with selectable `nova`, `matrix`, `sunset`, `ocean` and `mono` palettes.
- Different colors for directories and files in Tab-completion results.
- Persistent command history in `~/.nova_history` (Up/Down navigation).
- Interactive cursor movement, Backspace, Ctrl-C and Ctrl-D.
- UTF-8 command-line input for non-English text from Android keyboards.
- Tab completion for filesystem paths.
- Line-oriented built-in editor (`edit FILE`).
- Workspace metadata helpers (`workspace init`, `workspace info`).
- Kernel project scaffold helpers (`kernel init`, `kernel status`); these do not build, flash, or inspect a device kernel.
- Environment commands: `export NAME=VALUE`, `unset NAME`, and `env`.
- Simple `$NAME` and `${NAME}` expansion in command arguments.
- Session-local aliases with `alias NAME=COMMAND` and `unalias NAME`.
- Tests and clear documentation of supported behavior.

## Design boundary

NOVA Terminal is a command environment, not a replacement for a POSIX shell yet. Shell features such as pipelines, redirection, quoting rules and job control must not be assumed unless explicitly implemented and tested.

## License

License has not been selected yet.
