# ps4_new_order

GoldHEN payloads for the **PS4 9.00** that **reorder and show/hide the home-screen
tiles** by patching PlayStation's application database (`app.db`) directly on
disk — no kernel patching, no SceShellUI memory hacks.

The point: the PS4 home screen sorts its content tiles by an integer column
(`sortPriority`) in `app.db`. By rewriting a few of those integers we can, for
example, **put Library first** so the cursor lands on your first game instead of
the second tile, push the built-in system apps to the back, and hide tiles you
don't want (or bring back one you do).

## Payloads

| Folder | Output | What it does |
|--------|--------|--------------|
| `hide_lib/` | `New Order.bin` | Moves **Library** (`NPXS20111`) to `sortPriority 6` (front), pushes every other native app to `127` (behind games), hides **PS Store** (`NPXS20979`) and **Live from PlayStation** (`NPXS20105`). *What's New* and the disc tile are left where they are. |
| `revert_hide_lib/` | `New Order Revert.bin` | Restores a **stock** layout: Library → `1000`, PS Store → `6`, every other native app → `100`, and makes the Store / What's New / Live tiles visible again. Run this first for a clean base. |
| `hide_xplore/` | `Hide Xplore.bin` | Hides the **PS4-Xplorer** tile (`LAPY20009`): `visible 1 → 0`. |
| `show_xplore/` | `Show Xplore.bin` | Shows it again: `visible 0 → 1`. |
| `appdbdump/` | `appdbdump_v2.bin` | Copies `/system_data/priv/mms/app.db` to the USB stick (`PS4/DB/app.db`) so you can back it up / inspect it with any SQLite tool. |

Typical use: **`New Order Revert` → `New Order`**. The Xplore pair toggles
independently.

## How it works

Everything happens in `/system_data/priv/mms/app.db`:

* The home-screen content lives in a table named `tbl_appbrowse_<id>` — **one
  table per PSN account** (the `<id>` suffix changes per account). Every payload
  finds **all** tables whose name starts with `tbl_appbrowse` and patches each,
  so it works on consoles with multiple users.
* Each tile is one row, keyed by `titleId`. Two columns matter:
  * `sortPriority` (INT) — ordering within the content area.
  * `visible` (INT) — `1` shown, `0` hidden.
* The payloads do a **pure in-place SQLite B-tree record patch**: they walk the
  file's B-tree, locate the row, and overwrite the value bytes **without
  changing the record length**, then bump the database change counter (byte 24)
  and `fsync`. Because the serial type is kept identical, the B-tree pages never
  have to be rebalanced, so the patch is safe and atomic.

Known `titleId`s:

| titleId | Tile |
|---------|------|
| `NPXS20111` | Library (內容保存庫) |
| `NPXS20108` | What's New |
| `NPXS20109` | Disc (光碟) |
| `NPXS20979` | PlayStation Store |
| `NPXS20105` | Live from PlayStation |
| `NPXS20102` / `NPXS20104` | Internet Browser / Capture Gallery |
| `LAPY20009` | PS4-Xplorer |

## Requirements

* A **PS4 on 9.00** running **GoldHEN** (or another payload loader that loads
  `.bin` files from a USB drive's `payloads/` folder).
* A FAT/exFAT USB stick with a `payloads/` folder.

## Repository layout

Every payload is the same three files:

```
<payload>/
├── main.c        the payload source (the actual patch logic)
├── Makefile      build rules -> produces the .bin in this folder
└── debug_sym.c   two debug globals required by the SDK crt0 (unused here)
```

## Build

### Prerequisites

* A Linux host (or WSL on Windows).
* The **Scene-Collective ps4-payload-sdk** — provides `libPS4` (prebuilt
  `libPS4.a`), `crt0.s`, the linker script and the syscall wrappers:

  ```sh
  git clone --recursive https://github.com/Scene-Collective/ps4-payload-sdk
  ```

* A `gcc` able to emit x86-64 with `-march=btver2` (any reasonably modern host
  GCC works — the target library is prebuilt and only linked in).

### Configure the SDK path

The `Makefile` takes the SDK location from the `LIBPS4` variable (it defaults
to a placeholder). Point it at your clone — either edit the first line of each
`Makefile`, or pass it on the command line:

```sh
export LIBPS4=/path/to/ps4-payload-sdk/libPS4
```

`LIBPS4` must contain `crt0.s`, `linker.x`, `libPS4.a` and `include/ps4.h`.

### Compile

```sh
cd hide_lib          # or revert_hide_lib / hide_xplore / show_xplore / appdbdump
make                 # -> "New Order.bin" etc. in the same folder
```

Outputs:

| Directory | Binary |
|-----------|--------|
| `hide_lib` | `New Order.bin` |
| `revert_hide_lib` | `New Order Revert.bin` |
| `hide_xplore` | `Hide Xplore.bin` |
| `show_xplore` | `Show Xplore.bin` |
| `appdbdump` | `appdbdump_v2.bin` |

`make clean` removes the `build/` folder, the `.bin` and the linker `.map`.

## Usage

1. Copy the built `*.bin` into the USB stick's `payloads/` folder.
2. On the PS4, open the GoldHEN payload menu and run the payload.
3. **Reboot the console** (SceShell reads `app.db` at startup) — or at least
   return to the home screen — for the new layout to appear.

Always run **`New Order Revert`** before an update if you want to start from a
pristine database.

## Credits / references

* **[ps-patch-system](https://github.com/illusion0001/ps-patch-system)** — the
  reference for editing `app.db`'s `tbl_appbrowse_*` tables (`visible = 0` to
  hide tiles). The per-account table discovery and the `_id`-suffix handling
  here follow that approach.
* **[Scene-Collective ps4-payload-sdk](https://github.com/Scene-Collective/ps4-payload-sdk)**
  — SDK, syscall wrappers and B-tree/file helpers used to build these payloads.
* **SQLite file format**
  (https://www.sqlite.org/fileformat2.html) — the B-tree / record / serial-type
  layout that the in-place patcher relies on.

## Disclaimer

These payloads modify a system database file on your console. They are provided
**as-is, for educational purposes**. Back up `app.db` (use `appdbdump`) before
using them, and use them on your own hardware at your own risk.
