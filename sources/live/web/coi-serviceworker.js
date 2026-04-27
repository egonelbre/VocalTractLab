// coi-serviceworker — opts the page into cross-origin isolation by
// installing a service worker that injects
//   Cross-Origin-Opener-Policy: same-origin
//   Cross-Origin-Embedder-Policy: require-corp
// onto every response. With those headers active, SharedArrayBuffer
// becomes available, which the AudioWorklet + WASM_WORKERS build of
// vtl_live needs to spin up its synthesis thread.
//
// Why a service worker: static hosts like GitHub Pages can't set
// custom HTTP headers themselves. emrun and most WSGI/Express dev
// servers can; production-on-Pages needs this workaround to give the
// browser the headers it would otherwise expect from the origin.
//
// First page load: the SW registers, claims the client, and the
// register() handler reloads the page so the next round-trip serves
// through the SW (and the emscripten wasm + .data fetches arrive with
// COOP/COEP set, unblocking SAB). Subsequent loads short-circuit on
// the crossOriginIsolated check.

(function () {
  // ---- Worker context ----------------------------------------------------
  // Detect via ServiceWorkerGlobalScope rather than `window`, so this file
  // can be loaded both as a normal <script> from shell.html AND as the SW
  // script body itself.
  if (typeof ServiceWorkerGlobalScope !== "undefined" &&
      self instanceof ServiceWorkerGlobalScope) {
    self.addEventListener("install", () => self.skipWaiting());
    self.addEventListener("activate", (event) =>
      event.waitUntil(self.clients.claim()));

    self.addEventListener("fetch", (event) => {
      const request = event.request;

      // Range requests and only-if-cached checks have specialised browser
      // semantics — passing them through unmodified avoids breaking media
      // playback or HTTP cache replays.
      if (request.cache === "only-if-cached" && request.mode !== "same-origin") {
        return;
      }

      event.respondWith(
        fetch(request)
          .then((response) => {
            if (response.status === 0) return response;
            const headers = new Headers(response.headers);
            headers.set("Cross-Origin-Embedder-Policy", "require-corp");
            headers.set("Cross-Origin-Opener-Policy", "same-origin");
            return new Response(response.body, {
              status: response.status,
              statusText: response.statusText,
              headers,
            });
          })
          .catch((err) => {
            console.error("coi-sw fetch error:", err);
          })
      );
    });
    return;
  }

  // ---- Browser context ---------------------------------------------------
  if (typeof window === "undefined") return;
  if (!("serviceWorker" in navigator)) return;
  // Already isolated (e.g. headers served by the origin) — nothing to do.
  if (window.crossOriginIsolated) return;

  const swUrl =
    (document.currentScript && document.currentScript.src) ||
    "coi-serviceworker.js";

  navigator.serviceWorker.register(swUrl, { scope: "./" }).then(
    () => {
      // Reload as soon as a service worker takes control of this client,
      // so the next round-trip flows through the SW and arrives with
      // COOP/COEP set. If a controller is already attached but we're
      // still not crossOriginIsolated (older registration without our
      // header injection), reload immediately.
      if (navigator.serviceWorker.controller) {
        window.location.reload();
        return;
      }
      navigator.serviceWorker.addEventListener("controllerchange", () => {
        window.location.reload();
      });
    },
    (err) => {
      console.error("coi-sw registration failed:", err);
    }
  );
})();
