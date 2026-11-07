# BlockCraft Server
Dedicated server for BlockCraft PE. Push to its own repo; Actions builds Linux / Windows / macOS binaries.

    bc_server [port=25566] [seed=random] [mode=1]     # mode 0 survival, 1 creative

World seed + every block edit are stored in `world.dat` (survives restarts). Clients generate terrain locally from the seed.
Open TCP port 25566 on your router/firewall, then in the game: Servers -> Add / Direct -> `your.ip:25566`.

## Protocol (UTF-8 lines, `\n` terminated)
client -> server: `HELLO name` | `POS x y z yaw` | `SET x y z id` | `CHAT text`
server -> client: `WELCOME id seed mode` | `JOIN id name` | `LEAVE id` | `POS id x y z yaw` | `SET x y z id` | `CHAT name: text`
