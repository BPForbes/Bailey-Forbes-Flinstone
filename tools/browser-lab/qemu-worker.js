// QEMU runs entirely in browser workers. No guest execution happens on the host.
const children = new Set();
const NativeWorker = self.Worker;
self.Worker = class extends NativeWorker {
  constructor(...args) { super(...args); children.add(this); }
  terminate() { children.delete(this); return super.terminate(); }
};
let stage = "startup"; let input = [], readable = new Set(), qmpLine = "", mod;
let termios = { iflag: 0, oflag: 0, cflag: 0, lflag: 0, cc: Array(32).fill(0) };
const encoder = new TextEncoder();
const pty = {
  get readable() { return input.length > 0; }, writable: true,
  read(size) { return input.splice(0, size); },
  write(bytes) {
    for (const byte of bytes) {
      if (byte === 10) {
        try { self.postMessage({ type: "qmp", data: JSON.parse(qmpLine) }); } catch (_) {}
        qmpLine = "";
      } else if (byte !== 13) qmpLine += String.fromCharCode(byte);
    }
  },
  onReadable(fn) {
    readable.add(fn);
    // Commands queued before QEMU attaches the QMP pty must wake the reader
    // once it registers; otherwise the first QMP line sits unread.
    if (input.length) queueMicrotask(fn);
    return { dispose() { readable.delete(fn); } };
  },
  onSignal() { return { dispose() {} }; },
  ioctl(op, value) {
    if (op === "TCGETS") return termios;
    if (op === "TCSETS") { termios = value; return 0; }
    if (op === "TIOCGWINSZ") return [80, 25];
    throw new Error(`Unsupported terminal operation: ${op}`);
  },
};
function sendInput(value) {
  input.push(...encoder.encode(JSON.stringify(value) + "\n"));
  for (const fn of [...readable]) fn();
}
self.onmessage = async ({ data }) => {
  try {
    if (data.type === "destroy") {
      for (const child of [...children]) child.terminate();
      self.postMessage({ type: "destroyed" }); self.close(); return;
    }
    if (data.type === "qmp") { sendInput(data.data); return; }
    if (data.type === "screen") {
      stage = "screen read";
      try {
        const raw = mod.FS.readFile("/screen.bin");
        const bytes = new Uint8Array(raw);
        self.postMessage({ type: "screen", bytes });
      } catch (error) {
        self.postMessage({ type: "diagnostic", text: "screen read: " + error.message });
        self.postMessage({ type: "screen", bytes: null });
      }
      return;
    }
    if (data.type !== "boot") return;
    const base = new URL("./vendor/qemu/", import.meta.url);
    const [{ default: init }, romResponse] = await Promise.all([
      import(new URL("out.js", base)), fetch(new URL("load-rom.data", base)),
    ]);
    if (!romResponse.ok) throw new Error(`BIOS download failed: ${romResponse.status}`);
    const rom = new Uint8Array(await romResponse.arrayBuffer());
    const bios = [["bios-256k.bin", 0, 262144], ["efi-virtio.rom", 262144, 422912],
      ["kvmvapic.bin", 422912, 432128], ["linuxboot_dma.bin", 432128, 433664],
      ["vgabios-stdvga.bin", 433664, 473088]];
    mod = await init({
      pty,
      locateFile: (path) => new URL(path, base).href,
      mainScriptUrlOrBlob: new URL("out.js", base).href,
      arguments: ["-machine", "pc", "-accel", "tcg,tb-size=32", "-m", String(data.memorySize / 1048576),
        "-L", "/rom", "-vga", "std", "-display", "none", "-nic", "none", "-no-reboot",
        "-chardev", "file,id=com1,path=/dev/serial", "-serial", "chardev:com1", "-qmp", "stdio",
        "-drive", "file=/flintstone.img,format=raw,if=ide", "-boot", "c"],
      preRun: [(module) => {
        stage = "mkdir rom"; module.FS.mkdir("/rom");
        stage = "write bios"; for (const [name, start, end] of bios) module.FS.writeFile(`/rom/${name}`, rom.subarray(start, end));
        stage = "write disk"; module.FS.writeFile("/flintstone.img", new Uint8Array(data.image));
        stage = "serial device"; module.FS.createDevice("/dev", "serial", null, byte => self.postMessage({ type: "serial", byte }));
      }],
      print: text => self.postMessage({ type: "diagnostic", text }),
      printErr: text => self.postMessage({ type: "diagnostic", text }),
      onAbort: text => self.postMessage({ type: "error", text: String(text) }),
      onExit: code => self.postMessage({ type: "error", text: `QEMU exited (${code})` }),
    });
  } catch (error) { self.postMessage({ type: "error", text: stage + ": " + error.message + " errno=" + error.errno + " " + error.stack }); }
};
