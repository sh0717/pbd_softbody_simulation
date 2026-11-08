// 앱 초기화 계층에서 실행 환경을 선택합니다. DOM 접근을 물리 어댑터와 분리합니다.
export async function loadCoreOptions() {
  const threaded =
    globalThis.crossOriginIsolated === true && typeof SharedArrayBuffer === 'function';
  const options = {};

  if (threaded) {
    const script = document.querySelector('script[src$="/pbd_core.js"]');

    if (!script) throw new Error('물리 엔진 스크립트 경로를 찾을 수 없습니다');

    const url = new URL('pbd_core_threaded.js', script.src).href;

    if (typeof globalThis.createPBDThreadedModule !== 'function') {
      await new Promise((resolve, reject) => {
        const loader = document.createElement('script');
        loader.src = url;
        loader.onload = resolve;
        loader.onerror = () => reject(new Error('병렬 물리 엔진을 불러오지 못했습니다'));

        document.head.append(loader);
      });
    }

    options.moduleFactory = globalThis.createPBDThreadedModule;
    options.mainScriptUrlOrBlob = url;
  }

  const encoded = threaded
    ? typeof PBD_THREADED_WASM_BASE64 === 'string'
      ? PBD_THREADED_WASM_BASE64
      : null
    : typeof PBD_CORE_WASM_BASE64 === 'string'
      ? PBD_CORE_WASM_BASE64
      : null;

  if (encoded)
    options.wasmBinary = Uint8Array.from(atob(encoded), (character) => character.charCodeAt(0));

  return options;
}
