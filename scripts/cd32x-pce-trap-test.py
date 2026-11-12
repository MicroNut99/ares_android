#!/usr/bin/env python3
"""cd32x-pce-trap-test.py <dracx|gotx> <out dir> - a launcher that makes the PC Engine CD cores' chip 1 traps fire.

README-FPGA-FINDINGS.md section 6. Writes into <out dir>:
  launcher.bin        a 14 MB EverDrive image with a hand-assembled 68000 program: it writes a made-up disc table
                      and the core's key into the load port, reads the start, then reads and writes bank 20
                      (PSRAM chip 1) - what the console's cart does not serve once its engine runs
  mega-pro.rbf        a placeholder: ares picks the model by the name in the file beside it
  mega-pro.rbf.ares   the model's name
No game data: the System Card's bank is left empty (the engine starts and runs nothing useful).

Run: ares-headless --game <any CD32X .cue> --machine cd32x --bios-us <bios> --ramcart-type everdrive
       --ramcart-boot <out dir>/launcher.bin --frames 300
Expect: "[<core> TRAP] the console read PSRAM chip 1 (bank 20 ..." and "... wrote PSRAM chip 1 (bank 20) ...".
"""
import os, struct, sys

CORES = {
    #        key      the 68000's first step                              load port   start
    'dracx': (b'DXON', '33FCA00000A130F0',   # move.w #$A000,$A130F0  write on; window 7 shows bank 7
              0x3C1000, 0x3C0080),
    'gotx':  (b'GTON', '33FC001A00A130F2',   # move.w #$001A,$A130F2  bank 26 into window 1 ($080000)
              0x0C1000, 0x0C0080),
}


def table(key):
    """the load port's 256 words (FwState's): a disc of two tracks, the key in word 0xFF"""
    w = [0] * 256
    w[12], w[13], w[14], w[15] = 1, 2, 20000, 4            # first, last, lead-out, its control
    w[16 + 4 * 1: 20 + 4 * 1] = [0, 3000, 150, 0]          # track 1: audio
    w[16 + 4 * 2: 20 + 4 * 2] = [3000, 17000, 150, 4]      # track 2: data
    w[255] = struct.unpack('>I', key)[0]
    return struct.pack('>256I', *w)


def main():
    core, out = sys.argv[1], sys.argv[2]
    key, first, port, start = CORES[core]
    code = bytes.fromhex(
        '46FC2700' + first +                   # move.w #$2700,sr
        '41F900010000' +                       # lea $10000,a0      the table
        '43F9%08X' % port +                    # lea <load port>,a1
        '303C00FF'                             # move.w #255,d0
        '32D8' '32D8' '51C8FFFA' +             # loop: move.w (a0)+,(a1)+ twice (a word's high half, then its low); dbra
        '3239%08X' % start +                   # move.w <start>,d1  the engine starts
        '33FC001400A130F2'                     # move.w #$0014,$A130F2  bank 20 into window 1
        '343900080000'                         # move.w $080000,d2      trap: read
        '33C200080000'                         # move.w d2,$080000      trap: write
        '60FE')                                # bra *
    rom = bytearray(14 * 1024 * 1024)
    struct.pack_into('>II', rom, 0, 0x00FFFE00, 0x200)
    for v in range(2, 64): struct.pack_into('>I', rom, 4 * v, 0x200)
    rom[0x100:0x110] = b'SEGA SSF        '
    rom[0x150:0x170] = ('%s TRAP TEST' % core.upper()).ljust(32).encode()
    rom[0x200:0x200 + len(code)] = code
    rom[0x10000:0x10400] = table(key)
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'launcher.bin'), 'wb').write(rom)
    open(os.path.join(out, 'mega-pro.rbf'), 'wb').write(b'placeholder (ares models the cart by mega-pro.rbf.ares)\n')
    open(os.path.join(out, 'mega-pro.rbf.ares'), 'w').write(core + '\n')


if __name__ == '__main__':
    main()
