# seqmute-12 = seqmute-11 + Shift+knob sends the CC to every track in use (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute12.led`
SHA-256 `6dc2570e476d5641d54bea310acb78bfde1741a76784e1bd4834145224523461`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.** Replaces seqmute-11.

## Change (user request)
In round-robin mode, turning a CC-page knob (banks 1-4, knobs 1-4) while Shift is held sends that knob's CC, with the same CC number and value,
on the keyboard channel and on each further track channel in use (knob 4 on the chord page = tracks 1..4): 4 tracks = channels
keyboard..keyboard+3, 3 tracks = keyboard..keyboard+2, and so on. With Shift released, or round robin off, nothing changes.

## How
All CC-page knob moves end in one stock routine (0x08011574, single caller 0x080050a8) that builds the message (status 0xB0 | channel,
CC number, value) and calls the send function stored in the object (`ldr r0,[r4]; blx r3`, 4 bytes at 0x080115c2). That pair is replaced
by `bl cc_fanout`. cc_fanout does the stock call unless round robin is on and Shift (0x200010d2) is held; then it sends the message once per
track with the channel replaced by keyboard channel + track (the knob's own channel is not used in that case). Display, takeover logic and
the unchanged-value early exit are untouched.

## Space
Code 1076 bytes at 0x08005334.. (limit 0x080059ec), 480 bytes at 0x0801f180 (limit 0x0801f400). Pool and 19-window scans: no outside reads
of replaced words.

## Emulation
`emu_rr_test.py` 135 PASS: stock routine vs this image identical with round robin off, or Shift released (calls and object state); Shift with
1-4 tracks sends exactly the expected messages on the expected channels; unchanged value sends nothing. Sustain 26 PASS, seq 0 FAIL, chord test
only the two intended differences.

## Unverified on hardware
Whether Shift+knob on the CC page does anything else on a real unit; fast knob turns send up to four messages per step (the output queues hold 35).
