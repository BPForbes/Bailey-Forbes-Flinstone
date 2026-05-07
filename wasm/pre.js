/* Emscripten --pre-js: augment the factory `Module`; avoid replacing a caller-supplied moduleArg. */
if (typeof Module === 'undefined') {
  var Module = {};
}
globalThis.Module = Module;
if (!Module.arguments || Module.arguments.length === 0) {
  Module.arguments = ['-Virtualization', '-y', '-vm'];
}

/**
 * Ensure the output `<pre>` is attached to `document.body` once the DOM is ready.
 * @param {HTMLElement} el - The element created by {@link flinstoneWasmEnsureOut}.
 */
function flinstoneWasmAttachOut(el) {
  function tryAppend() {
    if (typeof document === 'undefined' || !document.body) return;
    if (el.parentNode) return;
    document.body.appendChild(el);
  }
  if (typeof document === 'undefined') return;
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function flinstoneOutOnce() {
      document.removeEventListener('DOMContentLoaded', flinstoneOutOnce);
      tryAppend();
    });
  } else {
    tryAppend();
  }
}

/**
 * Return existing `#flinstone-wasm-out` or create it with shell styling and attach it.
 * @returns {HTMLElement|null}
 */
function flinstoneWasmEnsureOut() {
  if (typeof document === 'undefined') return null;
  var el = document.getElementById('flinstone-wasm-out');
  if (el) return el;
  el = document.createElement('pre');
  el.id = 'flinstone-wasm-out';
  el.setAttribute('aria-live', 'polite');
  el.style.whiteSpace = 'pre-wrap';
  el.style.fontFamily = 'ui-monospace, monospace';
  el.style.margin = '12px';
  el.style.padding = '12px';
  el.style.background = '#0d1117';
  el.style.color = '#3fb950';
  el.style.borderRadius = '6px';
  el.style.minHeight = '200px';
  flinstoneWasmAttachOut(el);
  return el;
}

(function () {
  var prevPrint = Module.print;
  Module.print = function (text) {
    if (typeof prevPrint === 'function') prevPrint(text);
    var el = flinstoneWasmEnsureOut();
    if (el) el.appendChild(document.createTextNode(text + '\n'));
    else if (typeof console !== 'undefined') console.log(text);
  };
  var prevPrintErr = Module.printErr;
  Module.printErr = function (text) {
    if (typeof prevPrintErr === 'function') prevPrintErr(text);
    var el = flinstoneWasmEnsureOut();
    if (el) el.appendChild(document.createTextNode('[stderr] ' + text + '\n'));
    else if (typeof console !== 'undefined') console.error(text);
  };
})();
