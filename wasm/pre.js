/* Emscripten --pre-js: shared Module for MODULARIZE factory; default argv; tee to DOM. */
var Module = (typeof globalThis.Module !== 'undefined' && globalThis.Module) ? globalThis.Module : {};
globalThis.Module = Module;
if (!Module.arguments || Module.arguments.length === 0) {
  Module.arguments = ['-Virtualization', '-y', '-vm'];
}

/**
 * Ensures the given element is appended to document.body once the document is available.
 *
 * If document is undefined this function does nothing. If the element is already in the DOM it is not re-appended.
 * If the document is already interactive or complete it attempts to append immediately and, if that fails, registers
 * a one-time DOMContentLoaded listener to retry. If the document is not yet ready it registers a one-time
 * DOMContentLoaded listener to append when ready.
 *
 * @param {HTMLElement} el - The element to append to document.body.
 */
function flinstoneWasmAttachOut(el) {
  function tryAppend() {
    if (typeof document === 'undefined' || !document.body) return;
    if (el.parentNode) return;
    document.body.appendChild(el);
  }
  if (typeof document === 'undefined') return;
  if (document.readyState === 'complete' || document.readyState === 'interactive') {
    tryAppend();
    if (!el.parentNode) {
      document.addEventListener('DOMContentLoaded', function flinstoneOutOnce() {
        document.removeEventListener('DOMContentLoaded', flinstoneOutOnce);
        tryAppend();
      });
    }
  } else {
    document.addEventListener('DOMContentLoaded', function flinstoneOutOnce() {
      document.removeEventListener('DOMContentLoaded', flinstoneOutOnce);
      tryAppend();
    });
  }
}

/**
 * Ensure a single `<pre id="flinstone-wasm-out">` element exists for capturing WASM stdout/stderr and returns it.
 *
 * Creates and schedules insertion of a styled, accessible `<pre>` element when `document` is available; if an element with the same id already exists, returns it unchanged.
 * @returns {HTMLElement|null} The existing or newly created `<pre id="flinstone-wasm-out">` element, or `null` when `document` is not available.
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
