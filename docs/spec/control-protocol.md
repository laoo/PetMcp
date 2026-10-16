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
| `power_up_failed` | `emu.reset` with `power` could not read the ROMs; the PET that ran runs on. |
| `invalid_region` | No memory region of that name, or `memory.write` was given one it cannot write: a ROM. |
| `invalid_range` | The bytes asked for, or given to `memory.write`, run past the end of the region, or past RAM and video RAM in CPU addresses. |
| `screenshot_failed` | The picture could not be encoded or written. |
| `shutting_down` | The desktop application is closing, and the request was not run. |

## 5. Methods

The PET is powered up when the emulator starts, if `--rom-dir` names a directory that holds its
ROMs under the names VICE gives them (`docs/architecture.md`, "The machine"). Every method but
`emu.status`, `emu.pause`, `emu.resume`, `emu.reset` with `power` and `memory.list_regions`
answers `no_machine` until it is.

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
| `error` | Without `running`: why it is not. |

### `emu.pause`, `emu.resume`

`emu.pause` stops the desktop application's own run. `emu.resume` starts it again. Both take no
parameters and answer `{}`.

`petmcp-cli` has no run of its own. It runs only on request. There, these only set what
`emu.status` says.

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
| `reason` | `completed`. |
| `cycles_executed`, `frames_executed` | Cycles the run took, to the instruction boundary it stopped at, and frame boundaries it passed. |

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
