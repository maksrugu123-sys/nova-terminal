# Build NOVA Terminal on Termux

1. Install the compiler and Make:

   ```sh
   pkg update
   pkg install clang make
   ```

2. Clone the repository and select the development branch:

   ```sh
   git clone https://github.com/maksrugu123-sys/nova-terminal.git
   cd nova-terminal
   git switch develop/0.2.0
   ```

3. Build and run:

   ```sh
   make clean
   make
   ./nova
   ```

4. Optional installation into the active Termux prefix:

   ```sh
   make install
   nova
   ```

To disable ANSI colors, run `NO_COLOR=1 ./nova`.
