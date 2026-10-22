// The "?" dialogs of the build page.

export const HELP_CONTENT = {
  rom: {
    title: 'Your Mac Plus ROM',
    html: `<p>The Jackintosh runs Apple's own Macintosh Plus ROM, so you bring it: a dump of a Mac Plus's ROM
      chips, or a ROM file you own. It must be the <strong>Plus v3</strong> ROM (the last revision,
      1986): 128&nbsp;KB, checksum <code>$4D1F8172</code>.</p>
      <p>The page reads the file in your browser and never uploads it. The image you download contains
      it, so keep that image for yourself.</p>
      <p>Not accepted: the Plus v1 and v2 ROMs, the 128K and 512K ROMs, the SE's and later ones. Their code
      differs where the Jackintosh patches it.</p>`,
  },
  machine: {
    title: 'Which Atari?',
    html: `<p>The image goes in the TOS ROM sockets through the SidecarTridge TOS Emulator, and its size is
      the size of the Atari's ROM space:</p>
      <ul>
        <li><strong>ST family</strong> (ST, STF, STFM, Mega ST): 192&nbsp;KB at <code>$FC0000</code>.</li>
        <li><strong>STE family</strong> (STE, Mega STE): 256&nbsp;KB at <code>$E00000</code>. Sound and, on the
          Mega STE, 16&nbsp;MHz with the cache.</li>
      </ul>`,
  },
  install: {
    title: 'Putting it on the TOS Emulator',
    html: `<ol>
        <li>Copy the <code>.img</code> to the TOS Emulator's storage (the <code>ROMEMUL</code> drive).</li>
        <li>Write its name in <code>DEFAULT.TXT</code> there, or choose it from the TOS Emulator's switcher.</li>
        <li>Eject the drive safely, then power the Atari on with a monochrome monitor.</li>
        <li>The Multi-device, running md-jackintosh, serves the disks from its microSD card: its menu
          shows at boot to choose them.</li>
      </ol>`,
  },
};
