# Local camera and microphone check

`camera-microphone.html` is the local preview fixture used for the October 21
Chromium capture investigation. It makes no network requests, recordings or
uploads. Copy it to the guest and open it in Chromium, select the intended
camera and microphone, then press **Start both**. **Stop** releases the tracks;
repeat to exercise reopening. The page displays track settings, frame count,
elapsed time and microphone RMS. The meter is scaled for visibility; the JSON
contains the actual RMS value.

This is a manual fixture, not an automated pass/fail suite. Permissions and
secure-context requirements still apply. The controlled native test used
`--use-fake-ui-for-media-stream` to auto-grant permission with real devices;
it did not use fake media input. Keep that test-only flag off ordinary website
sessions. Choose the physical USB microphone explicitly instead of assuming
the default AHI device records from it.

The validation harness additionally requested 5 fps and sampled metrics over
bounded sessions. This saved page uses the browser's default video constraints
unless a caller supplies such a harness. Its initial frame counter and audio
meter alone do not prove synchronization, sustained performance, remote
streaming or a functioning website.

See [the dated results](../../../docs/status.md).
