# The control protocol

This document specifies how a client controls the emulator: what it sends, and what it gets back.
Why there is one protocol is [0005](../decisions/0005-one-control-api.md). It is PGMEmu's protocol,
with the PET's methods.

There is one set of methods (§5). Two protocols carry them:

- **JSON lines**, in the format of §2 and §3, on stdio or TCP.
- **MCP** (Model Context Protocol), where each method is a tool, on stdio or HTTP.

A method takes the same parameters and gives the same result in both.

## 0. Quick start

Start a server on TCP, then send JSON lines from a shell. All connections control the same
emulator.

```sh
petmcp-cli --server tcp:7701 --rom-dir ../vice/data/PET &
printf '%s\n' '{"id":1,"method":"emu.run_until","params":{"screen_contains":"READY."}}' \
              '{"id":2,"method":"video.screen_text"}' | nc -w 5 127.0.0.1 7701
```

```json
{"id":1,"ok":true,"result":{"reason":"condition_met","cycles_executed":1460451,"frames_executed":88}}
{"id":2,"ok":true,"result":{"lines":["### COMMODORE BASIC ###                 ","...","READY.                                  ","..."],"character_set":"graphics"}}
```

`scripts/petmcp.py` sends one request per call. It prints the `result` as indented JSON. On an
error, it prints the error on stderr and exits with status 1.

```sh
scripts/petmcp.py --port 7701 emu.run_frames count=60
scripts/petmcp.py --port 7701 memory.read address=0x8000 size=40
```

For an AI agent, use MCP. On stdio, the client starts the server itself. The desktop application
serves MCP on HTTP, and the client connects to it.

```sh
claude mcp add petmcp -- /path/to/petmcp-cli --mcp --rom-dir /path/to/roms --card-dir /path/to/card
petmcp --mcp-http 7702 &                                   # the desktop application, then
claude mcp add --transport http petmcp http://127.0.0.1:7702/mcp
```

Other MCP clients take a configuration like this one:

```json
{"mcpServers":{"petmcp":{"command":"/path/to/petmcp-cli","args":["--mcp","--rom-dir","/path/to/roms"]}}}
```

## 1. Transports

### JSON lines on stdio

`petmcp-cli --server` reads requests on stdin and writes responses on stdout.

- Each request is one line with one JSON object. Each response is one line with one JSON object.
- Responses are written in the order of the requests. Each response is flushed.
- Blank lines are skipped. They get no response.
- A line can end with `\n` or `\r\n`. There is no limit on its length. A newline inside a string
  must be escaped, as JSON requires.
- stdout carries only responses. Logs go to stderr. The server writes one line about itself to
  stderr when it starts.
- The server stops when stdin ends.

### JSON lines on TCP

`petmcp-cli --server tcp:PORT` serves the same lines on TCP. It listens on 127.0.0.1 only. It runs
until the process is stopped.

- Any number of connections can be open.
- Each connection has its own stream of lines. Its responses come in the order of its requests.
- A connection ends when the client closes it.
- All connections control **the same** emulator. A disk that one client mounts is the disk that
  all clients see.
- Requests from all connections are answered one at a time. Each request is finished before the
  next one starts.

This lets a script or an agent keep one emulator across many short commands, as
`scripts/petmcp.py` does.

There is no authentication. Anyone who can open a connection on this machine controls the
emulator. They can also make it read and write files, by the paths they give to methods such as
`disk.mount`, `video.screenshot`, `audio.capture_start` and `state.save`.

### JSON lines from the desktop application

`petmcp --server tcp:PORT` serves the same lines from the desktop application, while its window is
open ([0012](../decisions/0012-network-transports.md)).

- Requests from all connections, and what is done in the window, are handled in turn on the
  emulation thread, between two frames. They act on the PET on screen.
- The window runs the PET by itself, a frame at a time. `emu.pause` stops that; requests are
  still answered. A breakpoint or a watchpoint that the window's own run reaches pauses it too.
- The sound of a run made on request is not played.

### MCP on stdio

`petmcp-cli --mcp` serves the Model Context Protocol on stdio. Messages are JSON-RPC 2.0, one per
line, framed like the JSON lines above.

- The server answers `initialize`, `ping`, `tools/list` and `tools/call`.
- `initialize` gives the protocol revision the client asks for, if it is one of `2025-11-25`,
  `2025-06-18`, `2025-03-26` or `2024-11-05`. Otherwise it gives `2025-11-25`. The only capability
  is `tools`. The server's name is `petmcp`, and its version is the one `emu.status` gives. The
  `instructions` are a short paragraph on how to begin.
- A message without an `id` is a notification. It gets no answer. `notifications/initialized` is
  one of these.
- Batches (JSON arrays) are not supported.
- Each method in §5 is a tool. The tool's name is the method's name with `_` in place of `.`:
  `emu.run_frames` becomes `emu_run_frames`. The tool's description and `inputSchema` are the method's. The tool's `arguments` are the method's
  `params`.
- A tool's answer is a `content` array. It holds the method's `result` as JSON text. When the
  result holds a PNG (`video.screenshot` without a `path`), the PNG comes first, as an `image` of
  type `image/png`, and it is removed from the text.
- When a method fails, the tool result has `isError` set. Its one text is `CODE: message`, with
  the codes of §4.

Only these cases are JSON-RPC errors:

| Code | When |
|---|---|
| -32700 | The line is not JSON. The answer has `id` `null`. |
| -32600 | Not a JSON-RPC 2.0 request: not an object, `jsonrpc` is not `"2.0"`, or `method` is missing or not a string. |
| -32601 | A method other than the four above. |
| -32602 | `tools/call` without a `name`, or with a name that is not a tool. |

### MCP on HTTP, from the desktop application

`petmcp --mcp-http PORT` serves MCP's Streamable HTTP transport from the desktop application, at
`http://127.0.0.1:PORT/mcp`.

- Each message is the body of a POST.
- The answer to a request is the body of the response, as `application/json`.
- A notification gets `202 Accepted`, with no body.
- A body that is not JSON gets `400`, with the -32700 error.
- The server does not stream. A GET gets `405`.
- The server does not issue an `Mcp-Session-Id`. All clients control the same emulator, as on TCP.
- A request with an `Origin` header that is not a page on `localhost` or `127.0.0.1` gets `403`. A
  request without `Origin` (from a program, not a browser) is served.

```sh
curl -s http://127.0.0.1:7702/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"emu_status","arguments":{}}}'
```

The application and its servers act on the PET on screen. What a client mounts or runs is what
the window shows, and what is dropped onto the window is what the clients see.

### Transcripts

`petmcp-cli --transcript FILE` answers the requests in FILE, one per line, and prints the answers
on stdout. Blank lines and lines that start with `#` are skipped. With `--mcp`, the lines are MCP
messages, and a notification has no answer.

With `--expect ANSWERS`, the answers are compared line by line with the lines of ANSWERS, instead
of being printed. The first difference is printed. The exit status is 0 when all lines are equal,
and 1 otherwise.

An answer holds the build's version as `devel`. So a recorded transcript reads the same whatever
build recorded it, and any build checks it in the same way. `tests/control/` holds the
transcripts CTest checks.

## 2. Requests

```json
{"id":1,"method":"emu.status","params":{}}
```

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | A non-negative integer, echoed in the response. The protocol does not require ids to be unique or increasing; a client that wants to match responses keeps them so. |
| `method` | yes | The method's name. |
| `params` | no | An object. A method that takes no parameters is given `{}` when it is left out. |

Other fields are ignored. Parameters that a method does not use are ignored.

Names are case-sensitive. So are parameter values that are names (`cpu`, `access`, a key).

A parameter that is a non-negative integer (an address, a size, a count) can be a JSON integer,
or a string of hex digits after `0x` or `$`: `"0x8000"`, `"$8000"`. Answers always give integers.
The `id` must be a JSON integer.

## 3. Responses

A request that succeeded:

```json
{"id":1,"ok":true,"result":{"version":"devel"}}
```

A request that failed:

```json
{"id":1,"ok":false,"error":{"code":"unknown_method","message":"Unknown method: emu.nonsense"}}
```

- Keys appear in the order shown.
- `error.code` is stable and meant for programs; `error.message` is for people and may change.
- A request whose `id` cannot be read is answered under `id` 0. That covers text that is not
  JSON, JSON that is not an object, and an `id` that is missing, negative or not an integer.
- Every request is answered. A malformed one never ends the session.

## 4. Error codes

| Code | Meaning |
|---|---|
| `bad_request` | The request is malformed: not JSON, not an object, `id` or `method` missing or of the wrong type, `params` not an object, or a parameter missing, of the wrong type or out of its bounds. |
| `unknown_method` | No method of that name exists. |
| `no_machine` | The method needs the PET running, and it is not: its ROMs could not be read. The message says why. |
| `power_up_failed` | `emu.reset` with `power`, or `emu.set_rom_directory`, could not read the ROMs; the PET that ran runs on. |
| `invalid_input` | An `input.` method was given a key name, or a text, the PET's keyboard has no key for. |
| `load_failed` | `program.load` could not read the file, or the program is shorter than its address or does not fit in RAM; or `disk.run`'s LOAD failed. |
| `state_failed` | A state file could not be written or read. |
| `capture_running` | `audio.capture_start` while a capture is running. |
| `capture_not_running` | `audio.capture_stop` with no capture running. |
| `capture_failed` | The capture's WAV could not be written. |
| `no_expansion` | `expansion.get_state` while the PET runs without the expansion board (`--no-expansion`). |
| `state_mismatch` | A state file is not a PET state, is of another build's layout, or of a PET with other ROMs. |
| `invalid_region` | No memory region of that name, or `memory.write` was given one it cannot write: a ROM. |
| `invalid_range` | The bytes asked for, or given to `memory.write`, run past the end of the region, or past RAM and video RAM in CPU addresses. |
| `screenshot_failed` | The picture could not be encoded or written. |
| `not_found` | `disk.card`, `disk.mount` or `disk.run` was given a path that is not a directory, or not a file. |
| `mount_failed` | The SD2PET would not enter the image; the message is its error channel's. |
| `load_timeout` | `disk.run`'s LOAD did not end within a minute of the PET's time. |
| `stopped` | `disk.run`'s runs were stopped by a breakpoint or a watchpoint before the LOAD ended. |
| `shutting_down` | The desktop application is closing, and the request was not run. |

## 5. Methods

The PET is powered up when the emulator starts, if `--rom-dir` names a directory that holds its
ROMs under the names VICE gives them (`docs/architecture.md`, "The machine"). `emu.set_rom_directory` powers it up from another directory. Every method but `emu.status`,
`emu.pause`, `emu.resume`, `emu.reset` with `power`, `emu.set_rom_directory` and
`memory.list_regions` answers `no_machine` until it is.

A run ends at an instruction boundary, where the 6502's registers are whole, never inside an
instruction or an interrupt's entry. Time is counted in cycles of the 6502's 1 MHz clock from
power-up.

### `emu.status`

Takes no parameters.

```json
{"id":1,"ok":true,"result":{"version":"devel","paused":false,"running":true,"cycles":1460458,"frame":88}}
```

| Field | Meaning |
|---|---|
| `version` | The emulator's version: the release tag it was built from, or `devel`. |
| `paused` | Whether the desktop application's own run is paused (`emu.pause`). Always present; `petmcp-cli` has no run of its own. |
| `running` | Whether the PET is powered up. |
| `cycles`, `frame` | With `running`: cycles since power-up, and frame boundaries passed. |
| `breakpoint`, `watchpoint` | Present when the last run stopped at one: the same object as in that run's answer. So a client that attaches to a window that stopped by itself sees where. |
| `error` | Without `running`: why it is not. |

### `emu.pause`, `emu.resume`

`emu.pause` stops the desktop application's own run. `emu.resume` starts it again. Both take no
parameters and answer `{}`.

`petmcp-cli` has no run of its own. It runs only on request. There, these only set what
`emu.status` says.

### `emu.set_rom_directory`

| Param | Meaning |
|---|---|
| `path` | A directory holding the ROMs under the names VICE gives them. |

Reads the ROMs from `path` from now on, and powers the PET up with them, as `emu.reset` with
`power` does. Answers `{}`. On failure the directory and the PET stay as they were.

Errors: `power_up_failed`, `bad_request`.

### `emu.reset`

| Param | Meaning |
|---|---|
| `power` | `true` to switch the PET off and on: the ROMs read again, RAM as at power-up, time from 0. Left out or `false`, the reset line is pulled instead: the 6502, the PIAs and the VIA reset, and RAM and the video run on. |

Answers `{}`. The 6502 resets at its next instruction boundary; run it to see the reset done.

Errors: `no_machine`, `power_up_failed`, `bad_request`.

### `emu.run_frames`, `emu.run_cycles`

| Param | Meaning |
|---|---|
| `count` | `emu.run_frames`: frame boundaries to run through, where the vertical blank begins, 60.1 a second. `emu.run_cycles`: cycles to run at least. |

```json
{"id":4,"ok":true,"result":{"reason":"completed","cycles_executed":998401,"frames_executed":60}}
```

| Field | Meaning |
|---|---|
| `reason` | `completed`; `breakpoint` when it stopped before an instruction with a breakpoint; `watchpoint` when it stopped after an instruction that made a watched access. |
| `cycles_executed`, `frames_executed` | Cycles the run took, to the instruction boundary it stopped at, and frame boundaries it passed. |
| `breakpoint` | With `reason` `breakpoint`: `{"address"}`. |
| `watchpoint` | With `reason` `watchpoint`: `{"address", "access": "read" or "write", "value", "pc"}`, `pc` being the instruction that made the access. |

Every run, `emu.run_until`, `input.type`, `input.press` and `emu.step` among them, stops at a
breakpoint or a watchpoint and answers so.

Errors: `no_machine`, `bad_request`.

### `emu.run_until`

| Param | Meaning |
|---|---|
| `pc` | Stop before the instruction at this address. |
| `screen_contains` | Stop when a line of the screen, as `video.screen_text` reads it, holds this text. The screen is read once a frame. |
| `timeout_cycles` | Cycles after which to give up; 20,000,000 (20 s) when left out. |

Give `pc`, `screen_contains` or both; with both, either stops it. Answers as `emu.run_frames`
does, with `reason` `condition_met`, or `timeout` when the time ran out first.

Errors: `no_machine`, `bad_request`.

### `memory.list_regions`

Takes no parameters. Answers `regions`, each `{"name", "size", "writable"}`.

| Region | Holds |
|---|---|
| `CPU` | The 6502's address space, 64 KB, read as the 6502 reads it: the I/O registers as they read without a read's side effects, and an address nothing answers as the byte the bus last carried. |
| `RAM` | 32 KB at `$0000`. |
| `VIDEO_RAM` | 1 KB at `$8000`, also seen at `$8400`. |
| `BASIC_ROM`, `EDITOR_ROM`, `KERNAL_ROM` | 8 KB at `$C000`, 2 KB at `$E000`, 4 KB at `$F000`. |
| `CHARACTER_ROM` | The character generator, 2 KB, which only the video reads: the graphics set, then the text set. |
| `EXPANSION_RAM0`, `EXPANSION_RAM1` | The expansion board's two 512 KB RAMs, whole, whichever 4 KB bank `$A000` and `$B000` show. Read only here; the 6502 writes them through their windows. |
| `CHARACTER_RAM` | The expansion board's 4 KB of character RAM, both 2 KB buffers, buffer 0 first. |

The expansion board's regions exist while the PET runs with the board; without it, reading one
answers `invalid_region`.

### `memory.read`

| Param | Meaning |
|---|---|
| `address` | The first byte's address in the region. |
| `size` | How many bytes, at most 65536. |
| `region` | A name from `memory.list_regions`; `CPU` when left out. |

```json
{"id":5,"method":"memory.read","params":{"address":"$8000","size":4}}
{"id":5,"ok":true,"result":{"region":"CPU","address":32768,"data_hex":"23232320"}}
```

`data_hex` holds two lowercase hex digits per byte, in address order.

Errors: `no_machine`, `invalid_region`, `invalid_range`, `bad_request`.

### `memory.write`

| Param | Meaning |
|---|---|
| `address` | The first byte's address in the region. |
| `data_hex` | The bytes, two hex digits each, upper or lower case. |
| `region` | `CPU`, `RAM` or `VIDEO_RAM`; `CPU` when left out. |

Writes as a debugger does: no bus cycle is made and no device runs. In CPU addresses the bytes
must lie in RAM or video RAM, `$0000` to `$87FF`. The whole range is checked first, so the write
happens completely or not at all. Answers `{"region", "address", "size"}`.

Errors: `no_machine`, `invalid_region`, `invalid_range`, `bad_request`.

### `cpu.get_state`

Takes no parameters.

```json
{"id":6,"ok":true,"result":{"pc":58013,"a":0,"x":0,"y":10,"s":242,"p":50,"flags":"nv-BdiZc","disasm":"LDA $9E","cycle":1460457}}
```

| Field | Meaning |
|---|---|
| `pc` | The instruction the 6502 runs next. |
| `a`, `x`, `y`, `s` | The registers. |
| `p`, `flags` | The status register as PHP pushes it, and as `NV-BDIZC`, a capital for each flag set. |
| `disasm` | The instruction at `pc`. |
| `cycle` | The cycle the instruction's fetch is in. |

Errors: `no_machine`.

### `cpu.disassemble`

| Param | Meaning |
|---|---|
| `address` | Where to start. |
| `count` | How many instructions, at most 1000. |

Answers `instructions`, each `{"address", "length", "bytes_hex", "text"}`, read as the 6502
would read them, without a read's side effects. Undocumented opcodes are marked with a `*`.

Errors: `no_machine`, `bad_request`.

### `video.screenshot`

The last complete picture: the 320 by 200 dots of the 40 by 25 characters, completed where
the vertical blank begins. Lit dots are white and the rest black; the colour of a phosphor is the
frontend's.

| Param | Meaning |
|---|---|
| `path` | Where to write it as a PNG. Left out, the PNG comes back in the answer. |

```json
{"id":7,"ok":true,"result":{"width":320,"height":200,"frame":88,"path":"boot.png"}}
{"id":8,"ok":true,"result":{"width":320,"height":200,"frame":88,"png_base64":"iVBORw0..."}}
```

`frame` counts the pictures completed since power-up.

Errors: `no_machine`, `screenshot_failed`.

### `video.screen_text`

Takes no parameters. Answers `lines`, the 25 lines of the screen, 40 characters each, and
`character_set`, `graphics` or `text` as VIA CA2 chooses. Each screen code is the character the
set on show draws: letters (capitals in the graphics set, both cases in the text set), digits and
punctuation as themselves, the graphics as the nearest Unicode character. Reverse video is not
marked.

Errors: `no_machine`.

### `input.type`

| Param | Meaning |
|---|---|
| `text` | What to type, UTF-8. |
| `hold_frames`, `gap_frames` | Frames each key is held, and let go before the next: 1 to 600, 2 each when left out. |

Types `text` on the PET's keyboard, a key at a time, running the PET as it goes; the editor scans
the keyboard once a frame and takes a key again only after it has seen it up. Answers as
`emu.run_frames` does, with `keystrokes`, the keys typed.

- A letter of either case is the unshifted key, which makes a capital in the graphics set the
  3032 starts in ([0015](../decisions/0015-letters-keep-the-pets-shift.md)).
- Digits, punctuation and space are their keys; `"` is a key of its own on the PET. `←`, `↑` and
  `π` are the PET's own, and `_` and `^` stand for `←` and `↑`. `\n` and `\r` are RETURN.
- In braces, the keys no character names: `{RETURN}`, `{CLR}`, `{HOME}`, `{DEL}`, `{INST}`,
  `{UP}`, `{DOWN}`, `{LEFT}`, `{RIGHT}`, `{RVS}`, `{OFF}`, `{STOP}`, `{RUN}`, `{PI}`, and any key
  with SHIFT held over it, `{SHIFT-A}`.

```json
{"id":9,"method":"input.type","params":{"text":"print 2+2\n"}}
{"id":9,"ok":true,"result":{"reason":"completed","cycles_executed":665599,"frames_executed":40,"keystrokes":10}}
```

Errors: `no_machine`, `invalid_input`, `bad_request`.

### `input.press`

| Param | Meaning |
|---|---|
| `key` | The key's name: a character printed on it (`A`, `7`, `"`), or `RETURN`, `SPACE`, `LEFT_SHIFT`, `RIGHT_SHIFT`, `RVS`, `STOP`, `HOME`, `DEL`, `CRSR_DOWN`, `CRSR_RIGHT`, `LEFT_ARROW`, `UP_ARROW`. |
| `shift` | Hold the left SHIFT with it; `false` when left out. |
| `frames` | Frames to hold it, 1 to 600; 2 when left out. It is then let go for 2. |

Runs the PET as it goes, and answers as `emu.run_frames` does.

Errors: `no_machine`, `invalid_input`, `bad_request`.

### `input.set`, `input.clear`, `input.get_state`

`input.set` holds a key down (`down` true) or lets it go, by `key` as `input.press` names it, until
told otherwise; `input.clear` lets every such key go. Neither runs the PET; both answer `{}`.
`input.get_state` answers `held`, the names of the keys `input.set` holds, and `matrix`, the ten
rows of the keyboard's matrix as numbers, a set bit a key down.

The desktop application's keyboard holds keys of its own beside these; the PET sees both.

Errors: `no_machine`, `invalid_input`, `bad_request`.

### `program.load`

| Param | Meaning |
|---|---|
| `path` | A `.prg` file: its load address, little-endian, then its bytes. |
| `data_hex` | The same bytes, two hex digits each, instead of a file. |
| `run` | Run it too; `false` when left out. |

Give `path` or `data_hex`. Puts the program into RAM at its load address as BASIC's LOAD does, and
sets where BASIC's variables begin after it, so that RUN runs it. With `run`, types `RUN` for a
program at `$0401`, BASIC's start, and `SYS` to its first byte for any other, into the kernal's
keyboard buffer: the PET runs it as it next runs from READY. Load it from READY: before the PET
has booted, the kernal clears what was loaded.

```json
{"id":10,"ok":true,"result":{"start":1025,"end":1522,"typed":"RUN"}}
```

`end` is one past its last byte; `typed` is what was typed, with `run` only.

Errors: `no_machine`, `load_failed`, `bad_request`.

### `emu.step`

| Param | Meaning |
|---|---|
| `count` | Instructions to run, 1 to 1,000,000; 1 when left out. |

Answers as `emu.run_frames` does. An interrupt's entry is no instruction: it runs on the way to
the next one.

Errors: `no_machine`, `bad_request`.

### `debug.breakpoint.add`, `debug.breakpoint.remove`, `debug.breakpoint.list`

`add` and `remove` take `address`, an instruction's, and answer `{}`. A run stops before the
instruction there; the instruction the PET stands at runs when the run goes on, so a run never
stops twice at once at the same place. A breakpoint in code that runs every frame stops every
run within a frame; remove it to run on. `list` answers `{"breakpoints": [addresses]}`, ascending.

Errors: `no_machine`, `bad_request`.

### `debug.watchpoint.add`, `debug.watchpoint.remove`, `debug.watchpoint.list`

A watchpoint stops a run after the instruction that reads or writes data in its range: any access
the instruction makes but reading the instruction stream, its operands and the dummy reads of its
next opcode. Other dummy accesses, such as the read before an indexed access that crosses a page,
or the first write of a read-modify-write, reach the bus and count.

| Param | Meaning |
|---|---|
| `address` | The range's first address; one watchpoint per address, a second replacing the first. |
| `size` | Bytes in the range, 1 if left out. |
| `access` | `read`, `write` (the default) or `access` for both. |

`remove` takes `address`. `list` answers `{"watchpoints":[{"address","size","access"}]}`.

Errors: `no_machine`, `bad_request`.

### `debug.trace`

The history of execution, counted back from now: index 0 is the newest instruction, the one the
PET stands at and runs next, 1 the one that ran before it, and so on, over the last seconds
`debug.trace.configure` sets, 1 by default. While the PET stands still an index keeps naming the
same instruction; as it runs on, the indices move with it.

| Param | Meaning |
|---|---|
| `from` | The newest one's index; 0 when left out. |
| `count` | How many, going back from `from`, 0 to 4096; 32 when left out. |

```json
{"id":8,"method":"debug.trace","params":{"from":1,"count":2}}
{"id":8,"ok":true,"result":{"total":289167,"seconds":1.0,"instructions":[
  {"index":1,"pc":58013,"cycle":1660133,"a":0,"x":0,"y":10,"s":242,"p":50,"bytes_hex":"a59e","disasm":"LDA $9E"},
  {"index":2,"pc":58017,"cycle":1660130,"a":0,"x":0,"y":10,"s":242,"p":50,"bytes_hex":"f0fa","disasm":"BEQ $E29D"}]}}
```

| Field | Meaning |
|---|---|
| `total` | How many instructions are kept; the oldest has index `total` - 1. |
| `seconds` | The seconds kept. |
| `instructions` | Newest first, each with its `index`, the cycle of its fetch, the registers before it, and its bytes and disassembly as they were when it ran. |

Errors: `no_machine`, `bad_request`.

### `debug.trace.find`

Searches the history back from index `from`, the newest first, for the instructions that meet
every condition given, on the emulator's side: a minute of history, some 20 million instructions,
is searched through in about a second.

| Param | Meaning |
|---|---|
| `pc` | The instruction's address. |
| `pc_start`, `pc_end` | Instead of `pc`, both: the address is at least `pc_start` and below `pc_end`. |
| `disasm_contains` | Its disassembly, as it ran, holds this text, case aside: `STA $80`, `E812`, `JSR`. |
| `a`, `x`, `y`, `s` | The register held this before the instruction. |
| `from` | The index to search back from; 0, the newest, when left out. |
| `limit` | The most matches to answer, 1 to 4096; 32 when left out. |

```json
{"id":9,"method":"debug.trace.find","params":{"pc":"$E61B","limit":2}}
{"id":9,"ok":true,"result":{"total":339009,"matches":[
  {"index":5639,"pc":58907,"cycle":3307506,"a":0,"x":0,"y":10,"s":242,"p":50,"bytes_hex":"48","disasm":"PHA"},
  {"index":11280,"pc":58907,"cycle":3290867,"a":0,"x":0,"y":10,"s":242,"p":50,"bytes_hex":"48","disasm":"PHA"}],
  "next_from":11281}}
```

`matches` are as `debug.trace` answers its instructions, newest first. `next_from` is where to go
on searching back, with the same conditions, or `null` when the search reached the oldest kept.
Indices count back from the instruction the PET stands at, so they hold while it stands still.

Errors: `no_machine`, `bad_request`.

### `debug.trace.configure`

| Param | Meaning |
|---|---|
| `seconds` | The seconds of the PET's time the history keeps, 0.01 to 60. |

Applies to the PET running and to every one powered up after it. A second is some 300,000
instructions, 5 MB. Answers `{"seconds"}`.

Errors: `bad_request`.

### `io.get_state`

Takes no parameters. Answers the I/O chips' registers, read without a read's side effects:

| Field | Meaning |
|---|---|
| `pia1`, `pia2` | `port_a`, `ddr_a`, `control_a`, `port_b`, `ddr_b`, `control_b`, as written, and the control lines' outputs `ca2`, `cb2`. |
| `via` | `registers`, the 16 as written; `t1`, `t1_latch` and `t2`, the timers as they count; `ifr` and `ier`. |
| `irq` | Whether any of them pulls the IRQ line. |

Errors: `no_machine`.

### `state.save`, `state.load`, `state.list`

`state.save` writes the whole PET's state to `filename`, answering `{"filename", "size"}`;
`state.load` restores one. A `filename` without a directory is in the state directory
(`--state-dir`, or the working directory). `state.list` answers the `.petstate` files there,
`{"states":["a.petstate"]}`.

A state holds everything a run changes: the 6502, RAM and video RAM, the PIAs, the VIA with its
timers, the video's beam and pictures, the IEEE-488 port's lines, and the keys `input.set` holds.
It does not hold the SD2PET ([hardware/sd2pet.md](../hardware/sd2pet.md)): after a load the drive
waits for the PET afresh, its channels, directory and image as they were. It holds none of the ROMs,
the breakpoints, the watchpoints, the trace or the keys the desktop application's keyboard holds.
It is read back by the build that wrote it, into a PET with the same ROMs; any other is refused
with `state_mismatch`, and the PET runs on as it was. Loading a state, then running, gives exactly
the frames and memory that followed when it was saved.

Errors: `no_machine`, `state_failed`, `state_mismatch`, `bad_request`.

### `audio.capture_start`, `audio.capture_stop`

`audio.capture_start` keeps every frame of the PET's sound from then on, made by any run, and
answers `{}`. `audio.capture_stop` takes `path`, ends the capture and writes what it kept as a WAV,
answering `{"path", "frames", "seconds"}`.

The sound is stereo at 48 kHz, 16 bits. VIA CB2, filtered as VICE filters it (a low-pass at 8 kHz
at the CPU's rate, a high-pass at 160 Hz), is on both channels; the expansion board's AY1 is on the
left and AY2 on the right, DC-blocked at 10 Hz as the board's coupling capacitors do.

Errors: `no_machine`, `capture_running`, `capture_not_running`, `capture_failed`, `bad_request`.

### `expansion.get_state`

Takes no parameters. Answers the expansion board's state
([hardware/expansion-board.md](../hardware/expansion-board.md)):

| Field | Meaning |
|---|---|
| `chips` | Two, AY1 then AY2: `registers`, the 16 as written, all 8 bits; `latch`, the register the address latch selects; `clock_hz`, 2,000,000 or 1,000,000 with the divider on. |
| `banks` | The 4 KB banks of RAM0 at `$A000` and of RAM1 at `$B000`. |
| `characters` | `rom` when the video draws the PET's own characters, `ram` when it draws the shown buffer. |
| `shown_buffer` | The character buffer the video shows, 0 or 1; the 6502 sees the other at `$9800`. |

Errors: `no_machine`, `no_expansion`.

### `disk.status`

Takes no parameters. Answers the SD2PET, device 8 on the IEEE-488 port
([hardware/sd2pet.md](../hardware/sd2pet.md)):

| Field | Meaning |
|---|---|
| `card` | The host directory standing for its SD card (`--card-dir`, `disk.card`); empty with no card. |
| `directory` | The directory current on the card, as a host path. |
| `image` | The disk image entered there, as a host path; empty when none is. |
| `device` | Its device number; `U0>n` changes it. |
| `status` | Its error channel's message as the PET would read it now, without reading it: `73,NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF,00,00` after power-up. |

Errors: `no_machine`.

### `disk.card`

| Param | Meaning |
|---|---|
| `path` | A host directory; empty to take the card out. |

Puts the directory in the SD2PET as its card, for this PET and every one powered up after it.
The drive starts afresh at the card's root, as after a card change. Answers as `disk.status`.

Errors: `no_machine`, `not_found`, `bad_request`.

### `disk.mount`, `disk.unmount`

`disk.mount` takes `path`, a disk image (D64, D71, D81, D80, D82, DNP; NODISKEMU knows them by
their size), anywhere on the host. The SD2PET enters it as `CD` of it would from its directory,
which becomes the current one: inside the card, or outside it, where the directory's parent is
the card's root. `disk.unmount` leaves it, as `CD←` does. Each answers as `disk.status`.

Errors: `no_machine`, `not_found`, `mount_failed`, `bad_request`.

### `disk.command`

| Param | Meaning |
|---|---|
| `command` | A DOS command: `CD:NAME`, `S:NAME`, `N:NAME,ID`, `U1 2 0 18 0`, ... |

Runs the command as if the PET had sent it on channel 15, at once and beside the bus, and answers
`{"status"}`, the error channel's message it leaves. Letters of either case are sent as the PET's
keyboard types them, the unshifted keys; `←` or `_` is the left arrow, `↑` the up arrow, `£` the
pound.

Errors: `no_machine`, `bad_request`.

### `disk.run`

| Param | Meaning |
|---|---|
| `path` | A disk image to enter first, as `disk.mount` does; the drive as it is when left out. |
| `file` | The file to load; `*`, the first, when left out. |

Runs a program from the SD2PET as a person would. Runs the PET to its READY. after power-up if it
is not there yet, types `LOAD"file",8` and RETURN through the keyboard buffer, runs the PET until
the screen shows READY. after LOADING, or an error, and types `RUN` into the keyboard buffer: the
PET runs the program as it next runs.

```json
{"id":5,"ok":true,"result":{"reason":"completed","cycles":2213419,"frames":133,"typed":"LOAD\"*\",8 RUN","drive":{"card":"","directory":"/disks","image":"/disks/game.d64","device":8,"status":"00, OK,00,00"}}}
```

Errors: `no_machine`, `not_found`, `mount_failed`, `load_failed` (the screen's error line),
`load_timeout`, `stopped`, `bad_request`.

### `ieee.trace`

| Param | Meaning |
|---|---|
| `count` | How many changes, 0 to 4096; 64 when left out. |
| `from` | The newest one's index; 0 when left out. |

The IEEE-488 port's changes, counted back from the newest, index 0. The last 65536 are kept. Each
is `{"index", "cycle", "data", "low", "pet", "drive"}`: the cycle it came in; the byte on the data
lines, `"$41"` (a low line is a 1, as the bus counts); the control lines low (`ATN DAV NRFD NDAC
EOI IFC SRQ`, those low only); and which of them the PET and the SD2PET each pull. The answer is
`{"length", "events"}`, `length` how many are kept.

Errors: `no_machine`, `bad_request`.
