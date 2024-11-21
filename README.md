# tiny-gea3-api
[![Tests](https://github.com/geappliances/tiny-gea3-api/actions/workflows/test.yml/badge.svg)](https://github.com/geappliances/tiny-gea3-api/actions/workflows/test.yml)

PlatformIO library for interacting with GE Appliances products supporting the full-duplex GEA3 serial protocol using the [`tiny`](https://github.com/ryanplusplus/tiny) HAL. Note that this does not support older GE Appliances products using the single-wire GEA2 interface.

## Components
### `tiny_gea3_interface`
Provides a simple interface for sending and receiving GEA3 serial packets.

### `tiny_erd_client`
Provides a simple interface for reading and writing addressable data (ERDs) over a GEA3 serial interface.

## `tiny_gea2_interface_single_wire`
Provides a simple interface for sending and receiving GEA2 serial packets on a half duplex setup.
