# Local control

A button box (a Stream Deck), a script or a streaming program **on the same PC** can start and stop
a PushFPS recording, save the instant replay, mark a moment, take a screenshot, switch a scene of
Streaming mode, and read whether PushFPS is playing and recording.

It is off unless you switch it on: **Settings → Recording → Local control**.

## What it is, and what it is not

- It listens on `127.0.0.1` only: this PC, never the network. Nothing is forwarded and no port is
  opened in a router or a firewall.
- Every request needs the token PushFPS made for it. The token is 32 random bytes, stored encrypted
  for your Windows user, and never shown on screen: **Copy token** puts it on the clipboard.
  **New token** replaces it, and whatever held the old one stops working at once. Treat it like a
  password.
- A request from a web page is refused whatever it carries, so a page open in your browser cannot
  drive PushFPS.
- Five wrong tokens within a minute lock it for half a minute. At most ten requests a second.
- It can do exactly what is listed below, each through the same function as the button in PushFPS.
  It cannot read a file, change a setting, see a stream key or a console account, or run anything.
  The status holds yes/no values, numbers and a word for the kind of source; no file name, no
  folder, no device name.

## Requests

The port is `18766` unless you chose another one in Settings.

| Request | What it does |
| --- | --- |
| `GET /v1/status` | What is going on (JSON, below) |
| `POST /v1/record/start` | Starts a recording with the settings chosen last (no dialog) |
| `POST /v1/record/stop` | Stops it |
| `POST /v1/replay/save` | Saves the instant replay (it has to be on) |
| `POST /v1/marker` | Marks this moment of the recording (see `<recording>-markers.txt`) |
| `POST /v1/screenshot` | Takes a screenshot |
| `POST /v1/scene/1` … `/9` | Switches to that scene of Streaming mode |

Every request carries the header `Authorization: Bearer <token>` and no body.

An action answers `{"ok":true}` when PushFPS took it and `{"ok":false,"error":"not possible right now"}`
when it could not (nothing is playing, no recording runs, the replay is off or has nothing to save yet,
the scene does not exist, a marker could not be written). For a replay and a screenshot, `ok` means the
request was accepted: the file is written a moment later, and PushFPS says on screen when it is saved.
A request that is refused answers with its reason: `401` no or wrong token, `403` not from this PC or from a web page,
`404` no such request, `405` wrong method, `429` too many or locked.

Status:

```json
{"ok":true,"app":"PushFPS","api":1,"version":"4.1.0","playing":true,"source":"capture",
 "frameGeneration":true,"fps":119.7,"latencyMs":14.2,"recording":true,"recordingSeconds":83,
 "markers":2,"replay":true,"streaming":false}
```

`fps` and `latencyMs` are `null` while they are not known. `source` is one of `capture`, `ps5`,
`moonlight`, `xbox`, `screen`, `file`, `url`, `image`, `frameinput`, or empty.

## Examples

PowerShell (paste the token once, it stays in the variable for this window):

```powershell
$token = Get-Clipboard
$headers = @{ Authorization = "Bearer $token" }
Invoke-RestMethod -Uri http://127.0.0.1:18766/v1/status -Headers $headers
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:18766/v1/record/start -Headers $headers
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:18766/v1/marker -Headers $headers
```

curl:

```
curl -H "Authorization: Bearer TOKEN" http://127.0.0.1:18766/v1/status
curl -X POST -H "Authorization: Bearer TOKEN" http://127.0.0.1:18766/v1/replay/save
```

Stream Deck: use a plugin that sends a web request (for example "API Request" or "Web Requests"),
set the method and address from the table, and add the header `Authorization` with the value
`Bearer ` followed by your token. Leave the body empty.

## Sending the picture to a second PC

Local control is for buttons, not for video. To get PushFPS's picture onto a streaming PC:

- **Same PC, into OBS:** add the *PushFPS* source (the OBS plugin in the release), or switch on the
  virtual camera (Settings → Recording) and pick *PushFPS Virtual Camera* as a video capture device.
- **Another PC on your network:** run a small RTMP server on the streaming PC (MediaMTX is one
  file), then in PushFPS choose **Go live → Custom RTMP** with `rtmp://<that PC's address>/live`
  and any stream key. In OBS on the streaming PC add a *Media Source* with the same address and
  key. Expect about a second of delay: it is a stream, encoded and decoded once more.
