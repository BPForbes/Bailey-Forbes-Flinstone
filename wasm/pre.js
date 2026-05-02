/* Emscripten --pre-js: default argv for in-page embedded VM; tee stdout/stderr to a DOM node when present. */
var Module = typeof Module !== 'undefined' ? Module : {};
if (!Module.arguments || Module.arguments.length === 0) {
  Module.arguments = ['-Virtualization', '-y', '-vm'];
}

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
  if (document.body) document.body.appendChild(el);
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
