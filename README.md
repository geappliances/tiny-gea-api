# tiny-gea-api
[![Tests](https://github.com/geappliances/tiny-gea-api/actions/workflows/test.yml/badge.svg)](https://github.com/geappliances/tiny-gea-api/actions/workflows/test.yml)

PlatformIO library for interacting with GE Appliances products supporting the half-duplex GEA2 serial protocol and the full-duplex GEA3 serial protocol using the [`tiny`](https://github.com/ryanplusplus/tiny) HAL.

## Components
### `tiny_gea3_interface`
Provides a simple interface for sending and receiving GEA3 serial packets.

### `tiny_gea2_interface`
Provides a simple interface for sending and receiving GEA2 serial packets on a half duplex setup.

### `tiny_gea3_erd_client`
Provides a simple interface for reading and writing addressable data (ERDs) over a GEA3 serial interface.

## Dev Environment
1. Clone the repo
2. Install Cpputest
3. Run tests with `make test`

### Installing Cpputest on Linux
1. Run `sudo apt install cpputest`.

### Installing Cpputest on MacOS
1. Run `brew install cpputest`
2. Add these lines to `~/.bash_profile` after `eval "$(/opt/homebrew/bin/brew shellenv)"` on macOS:
   ```
   export CPATH="$CPATH:$(brew --prefix)/include"
   export LIBRARY_PATH="$LIBRARY_PATH:$(brew --prefix)/lib"
   ```
