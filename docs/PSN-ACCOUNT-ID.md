# Your PSN Account ID (for PS5 Remote Play)

Pairing PushFPS with a PS5 needs three things: the PS5's address, the 8-digit number the PS5 shows under
*Settings → System → Remote Play → Pair Device*, and the **Account ID** of a PSN user who is signed in on
that PS5.

The Account ID is not your PSN name. It is a number Sony gives every account, usually written as 12
characters ending in `=` (for example `AbCdEfGhIjK=`), sometimes as a long number. It never changes, it is
not a password, and you need it only once.

## Get it through Sony's own sign-in

Type your PSN password only on Sony's own page: the address bar shows `sony.com` or
`sonyentertainmentnetwork.com`.

- **psn-account-id** ([github.com/metalito/psn-account-id](https://github.com/metalito/psn-account-id),
  MIT licence): a small open-source script for Windows PowerShell or Python. It opens Sony's sign-in page;
  after signing in you paste the address the browser lands on, and it prints your Account ID. It talks only
  to Sony and saves nothing. As with any script, look at it before you run it: it is one short file.
- **chiaki-ng** ([github.com/streetpea/chiaki-ng](https://github.com/streetpea/chiaki-ng)), the open-source
  Remote Play client PushFPS's PS5 streaming is built on: its console registration window has a sign-in to
  PlayStation that fills in the Account ID.

Copy the ID into the *PSN Account ID* box of PushFPS's PS5 streaming window, then type the 8 digits and
press *Pair and save*.

## Never paste an "NPSSO" code anywhere

Some Account ID websites ask for an **NPSSO** code, which you are told to copy from a Sony address
(`ca.account.sony.com/api/v1/ssocookie`). That code is your signed-in PSN session: whoever has it can act as
you on PSN and see your email, your real name and your purchases. PushFPS never asks for it, and you never
need it for Remote Play.

If you pasted it into a website: open your PlayStation account settings, choose **Sign out of all
devices**, change your password, and turn on 2-step verification.

The *Get Account ID* button of PushFPS 4.4.5 and earlier opens a third-party lookup website. Use the ways
above instead.
