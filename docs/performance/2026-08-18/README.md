# Complete-contract publication evidence — 2026-08-18

The fixed seed-42 complete-contract matrix ran under GCC 14.2.0 and Clang
20.1.8 on Linux 6.12.74 with an Intel Core i9-13900H. Both compilers produced
the final authoritative checksum `9496404445183332939`, framebuffer checksum
`15648935810629710496`, and identical Kitty/ANSI snapshot SHA-256
`84fb2fcaa37d9df020fb0e7a049b2970b50737f833534d5b05b3666e285cbfeb`.

The representative GCC report measured 157.889 ms of acceptance simulation
and 0.420 ms of application rendering at the remote 320x240 viewport. Those
measurements exclude the terminal and proxy.

The separate live path was captured for three seconds in Kitty 0.32.2 on the
available Xorg/RDP display with:

```bash
./build-gcc/apsis-drift --capture-seconds 3 --profile remote \
  --seed 42 --report REPORT.json
```

It presented 93 frames in 3.008 seconds (30.92 FPS), with application renderer
average/p95 of 2.36/3.56 ms, complete frame-work average/p95 of 4.41/6.18 ms,
and 12.14 MiB/s written through the Kitty path. This is evidence for the
documented remote terminal/proxy route, not a direct local-terminal latency
claim; no non-RDP display endpoint was available on this host.
