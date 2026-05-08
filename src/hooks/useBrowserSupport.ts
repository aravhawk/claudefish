"use client";

import { useCallback, useEffect, useRef, useState } from "react";

export interface BrowserSupportResult {
  /** Still running initial checks */
  checking: boolean;
  /** True when the browser / device passes all requirements */
  supported: boolean;
  /** Human-readable reason why the browser is unsupported (undefined when supported) */
  reason?: string;
  /** Detected user agent info for display */
  detected: {
    isMobile: boolean;
    browser: string | null;
    hasWasm: boolean;
    hasSimd: boolean;
    hasSharedArrayBuffer: boolean;
    hasCoopCoep: boolean;
  };
}

type SupportState =
  | { status: "checking" }
  | { status: "supported" }
  | { status: "unsupported"; reason: string };

function detectBrowser(ua: string): string | null {
  if (/edg\/\d+/i.test(ua)) return "Edge";
  if (/firefox\/\d+/i.test(ua)) return "Firefox";
  if (/safari\//i.test(ua) && !/chrome|chromium|crios/i.test(ua)) return "Safari";
  if (/chrome|chromium|crios/i.test(ua)) return "Chrome";
  if (/samsungbrowser/i.test(ua)) return "Samsung Internet";
  return null;
}

function checkSupport(): SupportState {
  // Mobile device detection
  const isMobile = /android|iphone|ipad|ipod|blackberry|mobile/i.test(
    navigator.userAgent,
  );
  if (isMobile) {
    return {
      status: "unsupported",
      reason:
        "Claudefish is not available on mobile devices. Please open this page on a desktop browser.",
    };
  }

  // WebAssembly
  if (typeof WebAssembly === "undefined") {
    return {
      status: "unsupported",
      reason:
        "Your browser does not support WebAssembly, which Claudefish requires to run its chess engine.",
    };
  }

  // SharedArrayBuffer
  if (typeof SharedArrayBuffer === "undefined") {
    const ua = navigator.userAgent.toLowerCase();
    const browser = detectBrowser(navigator.userAgent);

    // Safari < 16.4 doesn't have SAB
    if (browser === "Safari") {
      const match = ua.match(/version\/(\d+(?:\.\d+)?)/);
      const version = match ? parseFloat(match[1]) : 0;
      if (!match || version < 16.4) {
        return {
          status: "unsupported",
          reason:
            `Claudefish requires Safari 16.4 or later (you appear to be on ${version || "an older"}). Please update Safari to play.`,
        };
      }
    }

    return {
      status: "unsupported",
      reason:
        "Your browser does not support SharedArrayBuffer, which Claudefish needs for its engine.",
    };
  }

  // COOP/COEP headers — check via crossOriginIsolated
  if (!crossOriginIsolated) {
    return {
      status: "unsupported",
      reason:
        "This page could not be loaded in a secure isolated context. Make sure you are accessing the site over HTTPS with the correct security headers.",
    };
  }

  return { status: "supported" };
}

export function useBrowserSupport(): BrowserSupportResult {
  const [state, setState] = useState<SupportState>({ status: "checking" });
  const checked = useRef(false);

  const runCheck = useCallback(() => {
    if (checked.current) return;
    checked.current = true;

    const result = checkSupport();
    setState(result);
  }, []);

  useEffect(() => {
    // Small delay so the UI can flash briefly, avoiding jarring layout shifts
    const id = requestAnimationFrame(() => {
      runCheck();
    });
    return () => cancelAnimationFrame(id);
  }, [runCheck]);

  const ua = typeof navigator !== "undefined" ? navigator.userAgent : "";
  const isMobile = /android|iphone|ipad|ipod|blackberry|mobile/i.test(ua);

  return {
    checking: state.status === "checking",
    supported: state.status === "supported",
    reason: state.status === "unsupported" ? state.reason : undefined,
    detected: {
      isMobile,
      browser: detectBrowser(ua),
      hasWasm: typeof WebAssembly !== "undefined",
      hasSimd: typeof WebAssembly !== "undefined"
        ? "SIMD" in WebAssembly
        : false,
      hasSharedArrayBuffer: typeof SharedArrayBuffer !== "undefined",
      hasCoopCoep: typeof crossOriginIsolated !== "undefined" ? crossOriginIsolated : false,
    },
  };
}
